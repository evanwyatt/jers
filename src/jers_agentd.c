/* Copyright (c) 2018 Evan Wyatt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(__linux__)
#define _XOPEN_SOURCE 700
#endif

#define UNUSED(x) (void)(x)

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <grp.h>
#include <sys/prctl.h>
#include <glob.h>

#include "jers.h"
#include "logging.h"
#include "common.h"
#include "buffer.h"
#include "fields.h"
#include "cmd_defs.h"
#include "auth.h"
#include "proxy.h"
#include "json.h"

#define MAX_EVENTS 1024
#define INITIAL_SIZE 0x1000
#define REQUESTS_THRESHOLD 0x010000
#define DEFAULT_DIR "/tmp"
#define DEFAULT_CONFIG_FILE "/etc/jers/jers_agentd.conf"
#define DEFAULT_AGENT_SOCKET "/run/jers/agent.sock"
#define DEFAULT_PROXY_SOCKET "/run/jers/proxy.sock"
#define DEFAULT_JOB_SOCKET "/run/jers/job.sock"
#define DEFAULT_DAEMON_PORT 7000
#define DEFAULT_TMPDIR "/tmp"
#define JERS_RUNDIR "/run/jers"
#define RECONNECT_WAIT 20 // Seconds
#define ADOPT_SLEEP 2

struct runningJob;

const char * getFailString(int);
const char * getErrType(int jers_error);
void disconnectFromDaemon(void);

int handleJobAdoptConnection(struct connectionType * connection);
int handleJobAdoptRead(struct runningJob *j);

void jersRunJob() __attribute__((noreturn));

char * server_log = "jers_agentd";
int server_log_mode = JERS_LOG_DEBUG;

int job_stderr = STDERR_FILENO;
pid_t job_pid = -1;

volatile sig_atomic_t shutdown_requested = 0;

struct jobCompletion {
	int exitcode;
	time_t finish_time;
	struct rusage rusage;
};

/* Linked list of active jobs
 *  * Note: the pid refers to the pid of the jersJob program. */

struct runningJob {
	char * temp_script;
	pid_t pid;
	pid_t job_pid;
	jobid_t jobID;
	time_t start_time;
	int kill;
	int socket;
	struct jobCompletion job_completion;

	int adopted;
	int completion_read;
	struct connectionType connection;

	struct runningJob *next;
	struct runningJob *prev;
};

struct adoptJob {
	jobid_t jobid;
	pid_t pid;
	pid_t job_pid;

	time_t start_time;
};

struct agent {
	buff_t requests;
	buff_t responses;
	size_t responses_sent;

	msg_t msg;

	int daemon;
	int in_recon;

	unsigned char secret_hash[SECRET_HASH_SIZE];
	int secret;
	char * nonce;

	char * daemon_socket_path;

	char * daemon_host;
	int daemon_port;

	int daemon_fd;
	int event_fd;

	char *tmpdir;

	struct connectionType client_proxy;
	struct connectionType job_adopt;

	time_t next_connect;

	struct runningJob *jobs;
	int64_t running_jobs;
};

struct jersJobSpawn {
	jobid_t jobid;

	char * name;
	char * queue;
	char * shell;

	char * wrapper;
	char * temp_script;
	char * pre_command;
	char * post_command;

	uid_t uid;
	int nice;

	int64_t argc;
	char ** argv;

	int64_t env_count;
	char ** envs;

	int64_t res_count;
	char ** resources;

	char * stdout;
	char * stderr;

	struct user * u;
};

struct agent agent;

static int _strcpy_len(char *dst, const char *src) {
	int len = 0;

	for (len = 0; src[len]; len++)
		dst[len] = src[len];

	dst[len] = '\0';

	return len;
}

void job_sighandler(int signum, siginfo_t *info, void *ucontext) {
	/* This function needs to be signal safe, so functions like printf can not be used */
	char buff[4096];
	int len = 0;

	UNUSED(ucontext);

	len += _strcpy_len(buff + len, "*** JERS - Signal received: ");
	len += _strcpy_len(buff + len, getSignalName(signum));
	len += _strcpy_len(buff + len, " From PID:");
	len += int64tostr(buff + len, info->si_pid);
	len += _strcpy_len(buff + len, " UID:");
	len += int64tostr(buff + len, info->si_uid);

	buff[len++] = '\n';

	write(job_stderr, buff, len);

	/* Forward this signal to the entire process group */
	if (job_pid != -1)
		kill(-job_pid, signum);
}

void setup_job_sighandler(void) {
	struct sigaction sigact;

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
	sigact.sa_sigaction = job_sighandler;
	sigaction(SIGTERM, &sigact, NULL);
}

void loadConfig(char * config) {
	FILE * f = NULL;
	char * line = NULL;
	size_t line_size = 0;
	ssize_t len = 0;

	/* Populate the defaults before we start parsing the config file */
	agent.daemon_socket_path = strdup(DEFAULT_AGENT_SOCKET);
	agent.tmpdir = strdup(DEFAULT_TMPDIR);

	if (config == NULL)
		config = DEFAULT_CONFIG_FILE;

	f = fopen(config, "r");

	if (f == NULL) {
		if (errno == ENOENT) {
			print_msg(JERS_LOG_WARNING, "No configuration file, using defaults");
			return;
		}

		error_die("Failed to open config file: %s\n", strerror(errno));
	}

	while ((len = getline(&line, &line_size, f)) != -1) {
		line[strcspn(line, "\n")] = '\0';
		char *key, *value;

		if (splitConfigLine(line, &key, &value))
			continue;

		if (key == NULL || value == NULL) {
			print_msg(JERS_LOG_WARNING, "Skipping unknown line: %ld %s\n", strlen(line),line);
			continue;
		}
	
		if (strcmp(key, "daemon_socket") == 0) {
			free(agent.daemon_socket_path);
			agent.daemon_socket_path = strdup(value);
		} else if (strcmp(key, "daemon_port") == 0) {
			agent.daemon_port = atoi(value);
		} else if (strcmp(key, "daemon_host") == 0) {
			agent.daemon_host = strdup(value);
			if (agent.daemon_port == 0)
				agent.daemon_port = DEFAULT_DAEMON_PORT;
		} else if (strcmp(key, "default_tmpdir") == 0) { 
			if (access(value, F_OK) != 0) {
				print_msg(JERS_LOG_WARNING, "TMPDIR specified in configuration file does not exist: %s", value);
				print_msg(JERS_LOG_WARNING, "Using default TMPDIR: %s", agent.tmpdir);
			} else {
				free(agent.tmpdir);
				agent.tmpdir = strdup(value);
			}
		} else {
			print_msg(JERS_LOG_WARNING, "Skipping unknown config key: %s\n", key);
			continue;
		}
	}

	free(line);

	if (feof(f) == 0)
		error_die("Failed to load config file: %s\n", strerror(errno));

	fclose(f);
	return;
}


/* Open 'file', moving any existing file to 'file_YYYYMMDDhhmmss_nnn'
 * It will try 10 times over 10 seconds to create the logfile
 * The fd to the logfile, or -1 on error is returned */

int open_logfile(char * file) {
	int fd;
	int attempts = 0;
	int rename_attempts;
	char * newfilename = NULL;

	while (1) {

		fd = open(file, O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);

		if (fd != -1)
			return fd;
		else if (errno != EEXIST)
			return -1;

		if (++attempts > 10) {
			fprintf(stderr, "Failed to create file %s : Too many attempts\n", file);
			return -1;
		}

		/* Already exists, try to rename it */
		newfilename = malloc(strlen(file) + 20); // _YYYYMMDDhhmmss_???
		time_t t = time(NULL);
		struct tm * tm = localtime(&t);

		if (!tm) {
			free(newfilename);
			return -1;
		}

		/* Create a link between filename & newfilename to 'rename' that file */

		int len = sprintf(newfilename, "%s_%d%02d%02d%02d%02d%02d", file, 1900 + tm->tm_year, tm->tm_mon + 1,
			tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
		rename_attempts = 0;

		while (++rename_attempts <= 999) {
			if (link(file, newfilename) == -1) {
				if (errno != EEXIST && errno != ENOENT) {
					free(newfilename);
					return -1;
				}

				/* Try adding _nnn */
				sprintf(newfilename + len, "_%d", attempts);
				continue;
			}

			break;
		}

		if (rename_attempts > 999) {
			/* Wait a bit before trying again */
			sleep(1);
			free(newfilename);
			continue;
		}

		/* Should now have a hard link of the file. */

		if (unlink(file) == -1) {
			if (errno == ENOENT) {
				/* Someone else possibly removed/renamed the file first */
				free(newfilename);
				continue;
			}

			fprintf(stderr, "Failed to remove file %s to %s\n", file, newfilename);
			free(newfilename);
			return -1;
		}

		free(newfilename);
	}

	return -1;
}

int createTempScript(struct jersJobSpawn * j) {
	int i;
	int fd = open (j->temp_script, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);

	if (fd < 0) {
		fprintf(stderr, "Failed to create temporary script: %s\n", strerror(errno));
		return 1;
	}

	dprintf(fd, "#!%s\n", j->shell);

	if (j->pre_command)
		dprintf(fd, "%s\n", j->pre_command);

	write(fd, j->argv[0], strlen(j->argv[0]));
	for (i = 1; i < j->argc; i++) {
		dprintf(fd, " \"%s\"", j->argv[i]);
	}

	if (j->post_command)
		dprintf(fd, "\n\n%s\n", j->post_command);

	if (fchown(fd, j->uid, j->uid) != 0) {
		fprintf(stderr, "Failed to change owner of temporary script: %s\n", strerror(errno));
		close(fd);
		unlink(j->temp_script);
		return 1;
	}

	close(fd);
	return 0;
}

/* The adopt file is used by a restarting jers_agentd process to adopt running jobs */

const char *adoptFilename(jobid_t jobid) {
	static char adopt_file[PATH_MAX];

	sprintf(adopt_file, "%s/adopt/.%d.adopt", JERS_RUNDIR, jobid);
	return adopt_file;
}

int createAdoptFile(jobid_t jobid, pid_t jobPid, time_t start_time) {
	const char *filename = adoptFilename(jobid);

	FILE *f = fopen(filename, "w");

	if (f == NULL) {
		fprintf(stderr, "Failed to create adopt file '%s': %s\n", filename, strerror(errno));
		return 1;
	}

	fprintf(f, "%d %d %ld\n", getpid(), jobPid, start_time);
	fclose(f);

	return 0;
}

int loadAdoptFile(jobid_t jobid, pid_t *pid, pid_t *jobPid, time_t *start_time) {
	const char *filename = adoptFilename(jobid);
	char line[64];

	*pid = *jobPid = *start_time = 0;
	FILE *f = fopen(filename, "r");

	if (f == NULL)
		return 1;

	size_t len = fread(line, 1, sizeof(line), f);

	if (len <= 0) {
		print_msg_warning("Failed to read job adopt file '%s': %s\n", filename, strerror(errno));
		fclose(f);
		return 1;
	}

	fclose(f);

	/* Doesn't have a newline, so the file might not be complete */
	if (line[len - 1] != '\n')
		return 1;

	if (sscanf(line, "%d %d %ld\n", pid, jobPid, start_time) != 3)
		return 1;

	return 0;
}

void deleteAdoptFile(jobid_t jobid) {
	const char *filename = adoptFilename(jobid);
	unlink(filename);

	return;
}

/* jersRunJob() is essentially a wrapper around a users job to setup the environment,
 * capture rusage information, handle signals, etc.
 *
 * The status/usage information is passed back through to the jersagentd via a socket
 *
 * Note: This function does not return */

void jersRunJob(struct jersJobSpawn * j, struct timespec * start, int _socket) {
	pid_t jobPid;
	int stdout_fd, stderr_fd;
	struct timespec end, elapsed;
	int i;
	int launch_status = 0;

	setproctitle("jers_agentd[%d]", j->jobid);

	/* Close some sockets we inherited from our parent */
	close(agent.daemon_fd);
	close(agent.client_proxy.socket);
	close(agent.job_adopt.socket);
	close(agent.event_fd);

	/* Generate the temporary script we are going to execute, if no wrapper was specified. */
	if (j->wrapper == NULL) {
		if (createTempScript(j) != 0) {
			launch_status = JERS_FAIL_TMPSCRIPT;
			goto launch_cleanup;
		}
	}

	/* Drop our privs now, to prevent us from overwriting log files
	 * we shouldn't have access to. Also double check we aren't running a job as root */

	if (j->u->uid == 0) {
		fprintf(stderr, "Jobs running as root are NOT permitted.\n");
		launch_status = JERS_FAIL_INIT;
		goto launch_cleanup;
	}

	if (setgroups(j->u->group_count, j->u->group_list) != 0) {
		fprintf(stderr, "Failed to initalise groups for job %d: %s\n", j->jobid, strerror(errno));
		launch_status = JERS_FAIL_INIT;
		goto launch_cleanup;
	}

	if (setgid(j->u->gid) != 0) {
		perror("setgid");
		launch_status = JERS_FAIL_INIT;
		goto launch_cleanup;
	}

	if (setuid(j->u->uid) != 0) {
		perror("setuid");
		launch_status = JERS_FAIL_INIT;
		goto launch_cleanup;
	}

	if (getuid() == 0) {
		fprintf(stderr, "Job STILL running as root after dropping privs\n");
		launch_status = JERS_FAIL_INIT;
		goto launch_cleanup;
	}

	/* Redirect stdout/stderr before we spawn,
	 * so we can write a summary at the end of the job */

	if (j->stdout)
		stdout_fd = open_logfile(j->stdout);
	else
		stdout_fd = open("/dev/null", O_WRONLY);

	if (stdout_fd == -1) {
		fprintf(stderr, "Failed to open stdout logfile for jobid:%d stdout:'%s' error:%s\n",
			j->jobid, j->stdout, strerror(errno));
		launch_status = JERS_FAIL_LOGERR;
		goto launch_cleanup;
	}

	if (j->stderr) {
		if (j->stdout && strcmp(j->stdout, j->stderr) == 0) {
			/* stderr point to the same file, just reuse the fd */
			stderr_fd = stdout_fd;
		} else {
			stderr_fd = open_logfile(j->stderr);
		}
	} else {
		stderr_fd = stdout_fd;
	}

	if (stderr_fd == -1) {
		fprintf(stderr, "Failed to open stderr logfile for jobid:%d stderr:'%s' error:%s\n",
                        j->jobid, j->stderr, strerror(errno));
		launch_status = JERS_FAIL_LOGERR;
		goto launch_cleanup;
	}

	/* Open STDIN as /dev/null */
	int stdin_fd = open("/dev/null", O_RDONLY);

	if (stdin_fd < 0) {
		fprintf(stderr, "Failed to open /dev/null for stdin: %s\n", strerror(errno));
		launch_status = JERS_FAIL_INIT;
		goto launch_cleanup;
	}

	/* Start off in the users home directory */

	if (chdir(j->u->home_dir) != 0) {
		dprintf(stderr_fd, "Warning: Failed to change directory to %s: %s\n", j->u->home_dir, strerror(errno));
		dprintf(stderr_fd, "Attempting to use %s\n", DEFAULT_DIR);

		if (chdir(DEFAULT_DIR) != 0) {
			dprintf(stderr_fd, "Error: Failed to change directory to %s: %s\n", DEFAULT_DIR, strerror(errno));
			launch_status = JERS_FAIL_INIT;
			goto launch_cleanup;
		}
	}

	/* Fork & exec the job */
	jobPid = fork();

	if (jobPid < 0) {
		fprintf(stderr, "Fork failed: %s\n", strerror(errno));
		launch_status = JERS_FAIL_START;
		goto launch_cleanup;
	}

	if (jobPid == 0) {
		int k = 0;
		char * argv[j->argc + 3];

		setsid();

		if (j->nice)
			nice(j->nice);

		/* Add our variables into the environment, along with
		 * any the user has requested.
		 * - We can modify the cached version directly, as we
		 *   are in a forked child.  */

		/* Resize if needed */
		int to_add = 10 + j->argc + j->env_count + j->res_count;

		if (to_add >= j->u->env_size - j->u->env_count)
			j->u->users_env = realloc(j->u->users_env, sizeof(char *) * (j->u->env_count + to_add + 1));

		/* Add in TMPDIR first, so that we don't overwrite a user variable */
		if (j->u->env_count > 0)
			j->u->users_env[j->u->env_count] = j->u->users_env[0];

		if (asprintf(&j->u->users_env[0],"TMPDIR=%s", agent.tmpdir) <= 0)
			goto spawn_exit;

		j->u->env_count++;

		/* Add the users variables */
		for (i = 0; i < j->env_count; i++)
			j->u->users_env[j->u->env_count++] = j->envs[i];

		/* Now our generic job ones. So they can not be overwritten by the user */
		if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_JOBID=%d", j->jobid) <= 0) goto spawn_exit;
		if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_QUEUE=%s", j->queue) <= 0) goto spawn_exit;
		if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_JOBNAME=%s", j->name) <= 0) goto spawn_exit;
		if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_STDOUT=%s", j->stdout ? j->stdout : "/dev/null") <= 0) goto spawn_exit;
		if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_STDERR=%s", j->stderr ? j->stderr : j->stdout ? j->stdout : "/dev/null") <= 0) goto spawn_exit;
		if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_ARGC=%ld", j->argc) <= 0) goto spawn_exit;

		for (i = 0; i < j->argc; i++)
			if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_ARGV%d=%s", i, j->argv[i]) <= 0) goto spawn_exit;

		if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_RESCOUNT=%ld", j->res_count) <= 0) goto spawn_exit;

		for (i = 0; i < j->res_count; i++)
			if (asprintf(&j->u->users_env[j->u->env_count++],"JERS_RES%d=%s", i, j->resources[i]) <= 0) goto spawn_exit;

		j->u->users_env[j->u->env_count] = NULL;

		if (!j->shell)
			j->shell = j->u->shell;

		dup2(stdin_fd, STDIN_FILENO);
		dup2(stdout_fd, STDOUT_FILENO);
		dup2(stderr_fd, STDERR_FILENO);

		close(stdin_fd);
		close(stdout_fd);

		if (stdout_fd != stderr_fd)
			close(stderr_fd);

		argv[k++] = j->shell;

		if (j->wrapper) {
			int i;
			argv[k++] = j->wrapper;

			for (i = 0; i < j->argc; i++)
				argv[k++] = j->argv[i];
		}
		else {
			argv[k++] = j->temp_script;
		}

		argv[k++] = NULL;

		execvpe(argv[0], argv, j->u->users_env);
		perror("execv failed for child");
		/* Will only get here if something goes horribly wrong with execvpe().*/
spawn_exit:
		_exit(JERS_FAIL_START);

	}

	/* Create the adopt file, so in case of the jers_agentd being restarted/crashing
	 * while the job is running, the next instance of jers_agentd can adopt this job */
	createAdoptFile(j->jobid, jobPid, start->tv_sec);

	/* SIGTERM handler so we can write to the jobs logfile if we get a sigterm */
	job_stderr = stderr_fd;
	job_pid = jobPid;
	setup_job_sighandler();

	close(stdin_fd);

	struct jobCompletion job_completion = {0};
	int rc = 0, exit_code = 0, sig = 0, status;

	/* Send the PID of the job */
	do {
		status = _send(_socket, &jobPid, sizeof(jobPid));

		if (status != sizeof(jobPid))
			fprintf(stderr, "Failed to send PID of job %d to agent: (status=%d) %s\n", j->jobid, status, strerror(errno));
	} while (status == -1 && errno == EINTR);

	/* Wait for the job to complete */
	while (wait4(jobPid, &rc, 0, &job_completion.rusage) < 0) {
		if (errno == EINTR)
			continue;

		fprintf(stderr, "wait4() failed pid:%d? %s\n", jobPid, strerror(errno));
		_exit(JERS_FAIL_PID);
	}

	clock_gettime(CLOCK_REALTIME_COARSE, &end);
	timespec_diff(start, &end, &elapsed);

	if (WIFEXITED(rc)) {
		job_completion.exitcode = WEXITSTATUS(rc) | JERS_EXIT_STATUS;
		exit_code = WEXITSTATUS(rc);
	} else if (WIFSIGNALED(rc)) {
		job_completion.exitcode = WTERMSIG(rc) | JERS_EXIT_SIGNAL;
		exit_code = 128 + WTERMSIG(rc);
		sig = WTERMSIG(rc);
	}

	struct timespec user_cpu, sys_cpu;
	user_cpu.tv_sec = job_completion.rusage.ru_utime.tv_sec;
	user_cpu.tv_nsec = job_completion.rusage.ru_utime.tv_usec * 1000;
	sys_cpu.tv_sec = job_completion.rusage.ru_stime.tv_sec;
	sys_cpu.tv_nsec = job_completion.rusage.ru_stime.tv_usec * 1000;

	job_completion.finish_time = end.tv_sec;

	/* Write a summary to the log file and flush it to disk
	 * This output should be kept to 10 lines so tail works nicely */
	lseek(stdout_fd, 0, SEEK_END);
	dprintf(stdout_fd, "\n========= Job Summary =========\n");
	dprintf(stdout_fd, " Elapsed Time : %s\n", print_time(&elapsed, 1));
	dprintf(stdout_fd, " Start time   : %s\n", print_time(start, 0));
	dprintf(stdout_fd, " End time     : %s\n", print_time(&end, 0));
	dprintf(stdout_fd, " User CPU     : %s\n", print_time(&user_cpu, 1));
	dprintf(stdout_fd, " Sys CPU      : %s\n", print_time(&sys_cpu, 1));
	dprintf(stdout_fd, " Max RSS      : %ldKB\n", job_completion.rusage.ru_maxrss);
	dprintf(stdout_fd, " UID          : %d (%s)\n", j->u->uid, j->u->username);
	dprintf(stdout_fd, " Job ID       : %d\n", j->jobid);
	dprintf(stdout_fd, " Exit Code    : %d", exit_code);

	if (sig)
		dprintf(stdout_fd, " (Signal %d - %s)\n", sig, getSignalName(sig));
	else
		write(stdout_fd, "\n", 1);

	fdatasync(stdout_fd);

	do {
		status = _send(_socket, &job_completion, sizeof(struct jobCompletion));

		if (status != sizeof(struct jobCompletion))
			fprintf(stderr, "Failed to send completion to agent: (status=%d) %s\n", status, strerror(errno));
	} while (status == -1 && errno == EINTR);

	if (status == -1 && getppid() == 1) {
		struct sockaddr_un addr;
		int status = -1;
		int fd;
		int connect_attempts = 0;

		dprintf(stdout_fd, "*** JERS - Job has been orphaned, attempting to reconnect to jers_agentd to finish sending completion.\n");

		fd = socket(AF_UNIX, SOCK_STREAM, 0);

		if (fd < 0) {
			fprintf(stderr, "Failed to create socket to reconnect to agentd: %s\n", strerror(errno));
			goto launch_cleanup;
		}

		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;

		strncpy(addr.sun_path, DEFAULT_JOB_SOCKET, sizeof(addr.sun_path));
		addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

		while (1) {
			if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {

				if (connect_attempts == 0) {
					dprintf(stdout_fd, "*** JERS - Failed to connect to agentd job adoption socket: '%s' Will attempt retry at %d second intervals.\n",
						strerror(errno), ADOPT_SLEEP);
				}

				connect_attempts++;
				sleep(ADOPT_SLEEP);
				continue;
			}

			break;
		}

		if (connect_attempts != 0)
			dprintf(stdout_fd, "*** JERS - Connected to new jers_agentd process. Sending completion.\n");

		do {
			status = _send(fd, &job_completion, sizeof(struct jobCompletion));

			if (status != sizeof(struct jobCompletion))
				fprintf(stderr, "Failed to send completion to agent: (status=%d) %s\n", status, strerror(errno));
		} while (status == -1 && errno == EINTR);

		if (status != -1)
			dprintf(stdout_fd, "*** JERS - Completion details sent.\n");

		close(fd);
	}

	fdatasync(stdout_fd);
	close(stdout_fd);

	if (stderr_fd != stdout_fd)
		close(stderr_fd);

	print_msg(JERS_LOG_DEBUG, "Job: %d finished: %08x\n", j->jobid, job_completion.exitcode);

launch_cleanup:

	close(_socket);
	deleteAdoptFile(j->jobid);

	_exit(launch_status);
}

struct runningJob * removeJob (struct runningJob * j) {
	struct runningJob * next = NULL;

	next = j->next;

	if (j->prev)
		j->prev->next = j->next;

	if (j->next)
		j->next->prev = j->prev;

	if (agent.jobs == j)
		agent.jobs = j->next;

	agent.running_jobs--;

	return next;
}

void addJob (struct runningJob * j) {
	if (agent.jobs) {
		j->next = agent.jobs;
		agent.jobs->prev = j;
	}

	agent.jobs = j;
	agent.running_jobs++;
}

void addOrphanJob(jobid_t jobid, pid_t pid, pid_t job_pid, time_t start_time) {
	struct runningJob *j = calloc(1, sizeof(struct runningJob));

	j->jobID = jobid;
	j->pid = pid;
	j->job_pid = job_pid;
	j->start_time = start_time;
	j->socket = -1;

	j->adopted = 1;

	addJob(j);
}

static void _sendMsg(buff_t *b) {
	struct epoll_event ee;
	int add_epoll = 0;

	if (agent.responses_sent <= agent.responses.used)
		add_epoll = 1;

	buffAddBuff(&agent.responses, b);
	buffFree(b);

	/* Only set EPOLLOUT if we have didn't already have it set */
	if (!add_epoll)
		return;

	ee.events = EPOLLIN|EPOLLOUT;
	ee.data.ptr = &agent;

	if (epoll_ctl(agent.event_fd, EPOLL_CTL_MOD, agent.daemon_fd, &ee)) {
		print_msg(JERS_LOG_WARNING, "Failed to set epoll EPOLLOUT on daemon_fd");
	}

	return;
}

void sendResponse(buff_t *b) {
	closeResponse(b);
	_sendMsg(b);
	return;
}

void sendRequest(buff_t *b) {
	closeRequest(b);
	_sendMsg(b);
	return;
}

int send_start(struct runningJob * j) {
	buff_t b;
	print_msg(JERS_LOG_INFO, "Job started: JOBID:%d PID:%d", j->jobID, j->pid);

	initRequest(&b, AGENT_JOB_STARTED, 1);
	JSONAddInt(&b, JOBID, j->jobID);
	JSONAddInt(&b, JOBPID, j->pid);
	JSONAddInt(&b, STARTTIME, j->start_time);

	sendRequest(&b);

	return 0;
}

int send_job_initfail(jobid_t jobid, int status) {
	buff_t b;

	print_msg(JERS_LOG_WARNING, "JOBID %d failed to initialise: %d (%s)", jobid, status, getFailString(status));

	initRequest(&b, AGENT_JOB_COMPLETED, 1);
	JSONAddInt(&b, JOBID, jobid);
	JSONAddInt(&b, FINISHTIME, time(NULL));
	JSONAddInt(&b, EXITCODE, status | JERS_EXIT_FAIL);

	sendRequest(&b);

	return 0;
}

int send_completion(struct runningJob * j) {
	buff_t b;

	if (j->socket)
		close(j->socket);

	free(j->temp_script);

	/* Only send the completion if we are connected to the main daemon process */
	if (agent.daemon_fd == -1 || agent.in_recon) {
		j->pid = -1;
		return 0;
	}

	initRequest(&b, AGENT_JOB_COMPLETED, 1);

	print_msg(JERS_LOG_INFO, "Job complete: JOBID:%d PID:%d RC:%08x Adopted:%d\n", j->jobID, j->pid, j->job_completion.exitcode, j->adopted);

	JSONAddInt(&b, JOBID, j->jobID);
	JSONAddInt(&b, EXITCODE, j->job_completion.exitcode);
	JSONAddInt(&b, FINISHTIME, j->job_completion.finish_time);

	/* Usage info */
	JSONAddInt(&b, USAGE_UTIME_SEC,  j->job_completion.rusage.ru_utime.tv_sec);
	JSONAddInt(&b, USAGE_UTIME_USEC, j->job_completion.rusage.ru_utime.tv_usec);
	JSONAddInt(&b, USAGE_STIME_SEC,  j->job_completion.rusage.ru_stime.tv_sec);
	JSONAddInt(&b, USAGE_STIME_USEC, j->job_completion.rusage.ru_stime.tv_usec);
	JSONAddInt(&b, USAGE_MAXRSS,     j->job_completion.rusage.ru_maxrss);
	JSONAddInt(&b, USAGE_MINFLT,     j->job_completion.rusage.ru_minflt);
	JSONAddInt(&b, USAGE_MAJFLT,     j->job_completion.rusage.ru_majflt);
	JSONAddInt(&b, USAGE_INBLOCK,    j->job_completion.rusage.ru_inblock);
	JSONAddInt(&b, USAGE_OUBLOCK,    j->job_completion.rusage.ru_oublock);
	JSONAddInt(&b, USAGE_NVCSW,      j->job_completion.rusage.ru_nvcsw);
	JSONAddInt(&b, USAGE_NIVCSW,     j->job_completion.rusage.ru_nivcsw);

	sendRequest(&b);

	removeJob(j);
	free(j);
	return 0;
}

/* Sent upon connection to confirm who we are */

int send_login(void) {
	buff_t b;
	char host[256];

	gethostname(host, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';

	print_msg(JERS_LOG_INFO, "Sending login");

	initRequest(&b, AGENT_LOGIN, 1);
	sendRequest(&b);
	return 0;
}

int get_job_completion(struct runningJob * j) {
	int status;

	/* Get the PID if we haven't already. */
	if (j->job_pid == 0) {
		status = _recv(j->socket, &j->job_pid, sizeof(pid_t));

		if (status == -1 && (errno != EAGAIN && errno != EWOULDBLOCK))
			fprintf(stderr, "Failed to read pid from job %d\n", j->jobID);
	}

	/* Read in the completion details */
	status = _recv(j->socket, &j->job_completion, sizeof(struct jobCompletion));

	if (status == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/* Clear the pid for this job, we will try later to read in the completion information again later */
			j->pid = 0;
			return 1;
		} else {
			print_msg(JERS_LOG_WARNING, "Failed to read in job completion information for jobid:%d :%s", j->jobID, strerror(errno));
			j->job_completion.exitcode = JERS_FAIL_UNKNOWN | JERS_EXIT_FAIL;
		}
	}

	return 0;
}

/* Check for PID messages for any children we've started
 * For any job we have a PID for, check if they have terminated */

void check_children(void) {
	struct runningJob * j = NULL;
	int status;
	int child_status;
	pid_t pid;

	j = agent.jobs;

	while (j != NULL) {
		struct runningJob * next = j->next;

		if (j->job_pid == 0) {
			do {
				status = read(j->socket, &j->job_pid, sizeof(pid_t));
			} while (status == -1 && errno == EINTR);

			if (status == -1 && (errno != EAGAIN && errno != EWOULDBLOCK))
				fprintf(stderr, "Failed to read pid from job %d\n", j->jobID);

			/* Check if we've been asked to send a signal to a batch job.
			 * We might have gotten this request before we knew the PID */

			if (j->job_pid && j->kill) {
				print_msg(JERS_LOG_DEBUG, "Sending delayed signal to job:%d signum:%d\n", j->jobID, j->kill);

				/* Send the saved signal to the job */
				if (kill(-j->job_pid, j->kill)) {
					print_msg(JERS_LOG_WARNING, "Failed to signal JOBID:%d with SIGNUM:%d", j->jobID, j->kill);
				}
			}
		}

		/* Check for any children we have cleaned up, but didn't get the completion information from */
		if (j->pid == 0) {
			if (get_job_completion(j) == 0)
				send_completion(j);
		}

		/* Check for adopted child completion */
		if (j->adopted && j->completion_read == sizeof(struct jobCompletion)) {
			send_completion(j);
		}

		j = next;
	}

	/* Check to see if we any jobs have completed */
	while ((pid = waitpid(-1, &child_status, WNOHANG)) > 0) {
		for (j = agent.jobs; j; j = j->next) {
			if (j->pid == pid)
				break;
		}

		if (j == NULL) {
			fprintf(stderr, "Got completion for PID %d, but can't find a matching job?!\n", pid);
			continue;
		}

		/* Remove its temporary script */
		if (j->temp_script) {
			if (unlink(j->temp_script) != 0)
				print_msg(JERS_LOG_WARNING, "Failed to remove temporary script file for job:%d '%s': %s", j->jobID, j->temp_script, strerror(errno));

			free(j->temp_script);
			j->temp_script = NULL;
		}

		/* If a job was started correctly, the child will always return 0
		 * - Anything else indicates it failed to start correctly */

		if (child_status) {
			int fail_status = JERS_FAIL_UNKNOWN | JERS_EXIT_FAIL;

			if (WIFEXITED(child_status))
				fail_status = WEXITSTATUS(child_status) | JERS_EXIT_FAIL;
			else if (WIFSIGNALED(child_status))
				fail_status = WTERMSIG(child_status) | JERS_EXIT_SIGNAL;

			print_msg(JERS_LOG_WARNING, "Job %d failed to start: %08x\n", j->jobID, fail_status);
			j->job_completion.exitcode = fail_status;
			j->job_completion.finish_time = time(NULL);
			send_completion(j);
			continue;
		}

		if (get_job_completion(j) == 0)
			send_completion(j);
	}

	if (pid == -1 && errno != ECHILD)
		fprintf(stderr, "waitpid failed? %s\n", strerror(errno));

	return;
}

struct runningJob * spawn_job(struct jersJobSpawn * j, int * fail_status) {
	/* Setup a socketpair so the child can return the usage information */
	int sockpair[2];
	struct timespec start_time;
	struct runningJob * job = calloc(sizeof(struct runningJob), 1);

	*fail_status = 0;

	if (job == NULL) {
		print_msg(JERS_LOG_CRITICAL, "Failed to allocate memory while spawning job: %s", strerror(errno));
		*fail_status = JERS_FAIL_INIT;
		return NULL;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0, sockpair) != 0) {
		fprintf(stderr, "Failed to create socketpair: %s\n", strerror(errno));
		*fail_status = JERS_FAIL_INIT;
		free(job);
		return NULL;
	}

	/* Generate the temporary script name */
	if (j->wrapper == NULL) {
		asprintf(&job->temp_script, "/run/jers/tmp/jers_%d", j->jobid);

		if (job->temp_script == NULL) {
			print_msg(JERS_LOG_CRITICAL, "Failed to allocate memory for temp script name: %s", strerror(errno));
			*fail_status = JERS_FAIL_INIT;
			free(job);
			return NULL;
		}

		j->temp_script = job->temp_script;
	}

	/* Need to flush stdout/stderr otherwise the child may get
	 * a copy of the buffered output from the parent */
	fflush(stdout);
	fflush(stderr);

	clock_gettime(CLOCK_REALTIME_COARSE, &start_time);

	pid_t pid = fork();

	if (pid == -1) {
		fprintf(stderr, "FAILED TO FORK(): %s\n", strerror(errno));
		*fail_status = JERS_FAIL_INIT;
		free(job);
		return NULL;
	} else if (pid == 0) {
		/* Child */
		close(sockpair[0]);
		jersRunJob(j, &start_time, sockpair[1]);
		/* jersRunJob() does not return */
	}

	close(sockpair[1]);

	job->start_time = start_time.tv_sec;
	job->pid = pid;
	job->job_pid = 0;
	job->jobID = j->jobid;
	job->socket = sockpair[0];

	addJob(job);

	return job;
}

int start_command(msg_t * m) {
	struct jersJobSpawn j = {0};
	struct runningJob * started = NULL;
	int i, status = 0;

	msg_item * item = &m->items[0];

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : j.jobid = getNumberField(&item->fields[i]); break;
			case JOBNAME  : j.name = getStringField(&item->fields[i]); break;
			case QUEUENAME: j.queue = getStringField(&item->fields[i]); break;
			case UID      : j.uid = getNumberField(&item->fields[i]); break;
			case NICE     : j.nice = getNumberField(&item->fields[i]); break;
			case SHELL    : j.shell = getStringField(&item->fields[i]); break;
			case ENVS     : j.env_count = getStringArrayField(&item->fields[i], &j.envs); break;
			case ARGS     : j.argc = getStringArrayField(&item->fields[i], &j.argv); break;
			case PRECMD   : j.pre_command = getStringField(&item->fields[i]); break;
			case POSTCMD  : j.post_command = getStringField(&item->fields[i]); break;
			case STDOUT   : j.stdout = getStringField(&item->fields[i]); break;
			case STDERR   : j.stderr = getStringField(&item->fields[i]); break;
			case WRAPPER  : j.wrapper = getStringField(&item->fields[i]) ; break;
			case RESOURCES: j.res_count = getStringArrayField(&item->fields[i], &j.resources); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
		}
	}

	/* Lookup the user from our cache */
	j.u = lookup_user(j.uid, 1);

	if (j.u == NULL) {
		print_msg(JERS_LOG_WARNING, "Failed to find user for job:%d uid:%d\n", j.jobid, j.uid);
		status = JERS_FAIL_INIT;
	} else {
		started = spawn_job(&j, &status);

		if (started)
			send_start(started);
	}

	if (status) {
		/* The job failed to initalise, so never forked.
		 * We need to send a fail message here, as we don't have a child to check later */
		send_job_initfail(j.jobid, status);
	}

	free(j.name);
	free(j.queue);
	free(j.shell);
	free(j.pre_command);
	free(j.post_command);
	free(j.stdout);
	free(j.stderr);
	free(j.wrapper);
	freeStringArray(j.argc, &j.argv);
	freeStringArray(j.env_count, &j.envs);

	return 0;
}

int clearcache_command(msg_t *m) {
	UNUSED(m);

	clearCacheHandler(0);
	print_msg_info("Flagged cache clear");

	return 0;
}

int signal_command(msg_t * m) {
	jobid_t id = 0;
	int signum = 0;
	int i;
	struct runningJob * j = NULL;

	msg_item * item = &m->items[0];

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : id = getNumberField(&item->fields[i]); break;
			case SIGNAL   : signum = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
		}
	}

	if (id == 0) {
		print_msg(JERS_LOG_WARNING, "No job passed to signal?");
		return 0;
	}

	j = agent.jobs;
	while (j) {
		if (j->jobID == id)
			break;

		j = j->next;
	}

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got request to signal a job that is not running? %d", id);
		return 0;
	}

	if (j->job_pid == 0) {
		fprintf(stderr, "Got request to signal job %d, but it hasn't sent us its PID yet...\n", j->jobID);
		/* Set a flag so its killed as soon as we get the PID */
		j->kill = signum;
		return 0;
	}

	if (kill(-j->job_pid, signum) != 0)
		print_msg(JERS_LOG_WARNING, "Failed to signal JOBID:%d with SIGNUM:%d", id, signum);

	return 0;
}

int recon_command(msg_t * m) {
	int32_t count = 0;
	buff_t b;
	char * hmac = NULL;
	time_t datetime = 0;
	char datetime_str[32];

	struct runningJob * j = agent.jobs;

	msg_item * item = &m->items[0];

	if (item != NULL) {
		for (int i = 0; i < item->field_count; i++) { 
			switch(item->fields[i].number) {
				case MSG_HMAC: hmac = getStringField(&item->fields[i]); break;
				case DATETIME: datetime = getNumberField(&item->fields[i]); break;

				default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
			}
		}
	}

	if (agent.secret) {
		/* Check the daemon we connected to knows the shared secret */
		if (hmac == NULL) {
			print_msg(JERS_LOG_CRITICAL, "HMAC not provided in recon request");
			return 1;
		}

		if (datetime == 0) {
			print_msg(JERS_LOG_CRITICAL, "DATETIME not provided in recon request");
			return 1;
		}
		sprintf(datetime_str, "%ld", datetime);
		const char * recon_verify[] = {agent.nonce, datetime_str, NULL};
		char * verify_hmac = generateHMAC(recon_verify, agent.secret_hash, sizeof(agent.secret_hash));

		if (verify_hmac == NULL) {
			print_msg(JERS_LOG_CRITICAL, "Failed to generate HMAC during recon request.");
			return 1;
		}

		if (strcasecmp(verify_hmac, hmac) != 0) {
			print_msg(JERS_LOG_CRITICAL, "HMAC does not match - Disconnecting from daemon");
			free(verify_hmac);
			return 1;
		}

		free(verify_hmac);
	}

	/* The master daemon is requesting a list of all the jobs we have in memory.
	 * We will remove the jobs in memory only when the master daemon confirms it's processed the recon message */

	initNamedResponse(&b, AGENT_RECON_RESP, CONST_STRLEN(AGENT_RECON_RESP), 1, NULL);

	print_msg(JERS_LOG_INFO, "=== Start Recon ===\n");

	while (j) {
		print_msg(JERS_LOG_INFO, "JobID: %d PID:%d Adopted:%d ExitCode:%d StartTime:%ld FinishTime:%ld\n", j->jobID, j->pid, j->adopted, j->job_completion.exitcode, j->start_time, j->job_completion.finish_time);

		/* Add job to recon message request */
		JSONStartObject(&b, NULL, 0);
		JSONAddInt(&b, JOBID, j->jobID);
		JSONAddInt(&b, STARTTIME, j->start_time);

		switch (j->pid) {
			case -1:
				/* Job complete */
				JSONAddInt(&b, EXITCODE, j->job_completion.exitcode);
				JSONAddInt(&b, FINISHTIME, j->job_completion.finish_time);

				/* Usage info */
				JSONAddInt(&b, USAGE_UTIME_SEC,  j->job_completion.rusage.ru_utime.tv_sec);
				JSONAddInt(&b, USAGE_UTIME_USEC, j->job_completion.rusage.ru_utime.tv_usec);
				JSONAddInt(&b, USAGE_STIME_SEC,  j->job_completion.rusage.ru_stime.tv_sec);
				JSONAddInt(&b, USAGE_STIME_USEC, j->job_completion.rusage.ru_stime.tv_usec);
				JSONAddInt(&b, USAGE_MAXRSS,     j->job_completion.rusage.ru_maxrss);
				JSONAddInt(&b, USAGE_MINFLT,     j->job_completion.rusage.ru_minflt);
				JSONAddInt(&b, USAGE_MAJFLT,     j->job_completion.rusage.ru_majflt);
				JSONAddInt(&b, USAGE_INBLOCK,    j->job_completion.rusage.ru_inblock);
				JSONAddInt(&b, USAGE_OUBLOCK,    j->job_completion.rusage.ru_oublock);
				JSONAddInt(&b, USAGE_NVCSW,      j->job_completion.rusage.ru_nvcsw);
				JSONAddInt(&b, USAGE_NIVCSW,     j->job_completion.rusage.ru_nivcsw);
				break;

			case 0:
				/* Just finished, no completion info available yet. */
				break;

			default:
				/* Running */
				JSONAddInt(&b, JOBPID, j->pid);
				break;
		}

		JSONEndObject(&b);
		count++;
		j = j->next;
	}

	sendResponse(&b);

	agent.in_recon = 1;

	print_msg(JERS_LOG_INFO, "=== End Recon %d job/s ===\n", count);
	return 0;
}

/* Master daemon has acknowleged it has completed the recon 
 * We can now cleanup all the jobs that have completed. */

int recon_complete(msg_t * m) {
	struct runningJob * j = agent.jobs;

	UNUSED(m);

	while (j) {
		if (j->pid == -1) {
			struct runningJob * next = removeJob(j);
			free(j);
			j = next;
		} else {
			j = j->next;
		}
	}

	agent.in_recon = 0;
	return 0;
}

/* Handle a response to a proxied client command */
int proxy_response(msg_t *m) {
	pid_t pid = 0;
	char *data = NULL;
	msg_item * item = &m->items[0];

	for (int i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case PID: pid = getNumberField(&item->fields[i]); break;
			case PROXYDATA: data = getStringField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
		}
	}

	if (pid == 0)
		return 0;

	/* Locate the client structure */
	proxyClient * c;
	for (c = proxyClientList; c; c = c->next) {
		if (c->pid == pid)
			break;
	}

	if (c == NULL) {
		print_msg(JERS_LOG_WARNING, "Failed to locate proxy client for response pid: %d", pid);
		return 0;
	}

	buffAdd(&c->response, data, data ? strlen(data):0);
	pollSetWritable(&c->connection);

	free(data);

	return 0;
}

/* We have been requested to authenticate.
 * Use the shared secret we loaded earlier to generate a hmac of
 * a client nonce we generate, and the current datetime */

int auth_challenge(msg_t *m) {
	char * nonce = NULL;
	char time_now_str[32];
	time_t time_now = time(NULL);
	char * hmac = NULL;

	msg_item * item = &m->items[0];

	for (int i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case NONCE: nonce = getStringField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
		}
	}

	if (nonce == NULL) {
		print_msg(JERS_LOG_WARNING, "Incorrectly formatted auth_challenge request received. (No nonce)");
		return 1;
	}

	if (agent.secret == 0) {
		//HACK:
		if (loadSecret("/etc/jers/secret.key", agent.secret_hash)) {
			print_msg(JERS_LOG_WARNING, "Unable to connect to main daemon during auth_challenge - No secret has been loaded");
			free(nonce);
			return 1;
		}

		agent.secret = 1;
	}

	agent.nonce = generateNonce(NONCE_SIZE);

	sprintf(time_now_str, "%ld", time_now);

	const char *hmac_input[] = {nonce, agent.nonce, time_now_str, NULL};
	hmac = generateHMAC(hmac_input, agent.secret_hash, sizeof(agent.secret_hash));

	buff_t auth_resp;
	initRequest(&auth_resp, AGENT_AUTH_RESP, 1);

	JSONAddInt(&auth_resp, DATETIME, time_now);
	JSONAddString(&auth_resp, NONCE, agent.nonce);
	JSONAddString(&auth_resp, MSG_HMAC, hmac);

	sendRequest(&auth_resp);

	free(hmac);
	free(nonce);

	return 0;

}

/* Process a command */
int process_message(msg_t * m) {
	int status = 0;

	if (m->command == NULL) {
		print_msg(JERS_LOG_WARNING, "Got a command message");
		free_message(m);
		return 1;
	}

	print_msg(JERS_LOG_DEBUG, "Got command '%s'", m->command);

	if (strcmp(m->command, AGENT_START_JOB) == 0) {
		status = start_command(m);
	} else if (strcmp(m->command, CMD_SIG_JOB) == 0) {
		status = signal_command(m);
	} else if (strcmp(m->command, AGENT_RECON_REQ) == 0) {
		status = recon_command(m);
	} else if (strcmp(m->command, AGENT_RECON_COMP) == 0) {
		status = recon_complete(m);
	} else if (strcmp(m->command, AGENT_AUTH_CHALLENGE) == 0) {
		status = auth_challenge(m);
	} else if (strcmp(m->command, AGENT_PROXY_DATA) == 0) {
		status = proxy_response(m);
	} else if (strcmp(m->command, CMD_CLEAR_CACHE) == 0) {
		status = clearcache_command(m);
	} else {
		print_msg(JERS_LOG_WARNING, "Got an unexpected command message '%s'", m->command);
		status = 1;
	}

	free_message(m);

	return status;
}

/* Loop through processing messages. */
void process_messages(void) {
	char *p = agent.requests.data;
	size_t consumed = 0;

	if (agent.requests.used == 0)
		return;

	while (1) { 
		char *nl = memchr(p, '\n', agent.requests.used - consumed);

		if (nl == NULL)
			break;

		*nl = '\0';
		nl++;

		size_t request_len = nl - p;

		if (load_message(p, &agent.msg) != 0) {
			print_msg(JERS_LOG_WARNING, "Failed to load agent message");
			disconnectFromDaemon();
			break;
		}

		if (process_message(&agent.msg) != 0) {
			disconnectFromDaemon();
			break;
		}

		p += request_len;
		consumed += request_len;
	}

	if (consumed)
		buffRemove(&agent.requests, consumed, 0);
}

/* Connect to the main JERS daemon
 * If a host is specified in the configuration file try that, otherwise
 * connect locally using a unix socket */

int connectjers(void) {
	int fd;

	if (time(NULL) < agent.next_connect) {
		sleep(5);
		return 1;
	}

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (fd < 0) {
		print_msg(JERS_LOG_WARNING, "Failed to connect to JERS daemon (socket failed): %s", strerror(errno));
		goto connect_fail;
	}

	if (agent.daemon_host) {
		/* Get the details of the host we are trying to connect to */
		struct addrinfo hint = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
		struct addrinfo *info, *p;
		int status;
		char port[8];
		sprintf(port, "%d", agent.daemon_port);

		print_msg(JERS_LOG_INFO, "Attempting to connect JERS daemon on host %s:%d", agent.daemon_host, agent.daemon_port);

		/* Resolve the host to addresses we can connect to */
		if ((status = getaddrinfo(agent.daemon_host, port, &hint, &info)) != 0) {
			print_msg(JERS_LOG_WARNING, "Failed to resolve address of '%s': %s\n", agent.daemon_host, gai_strerror(status));
			goto connect_fail;
		}

		/* Connect to the first address we can that is associated with that servername & port */
		for(p = info; p != NULL; p = p->ai_next) {
			if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
				print_msg(JERS_LOG_WARNING, "Failed to create socket: %s", strerror(errno));
				continue;
			}

			if (connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
				print_msg(JERS_LOG_WARNING, "Failed to connect: %s", strerror(errno));
				close(fd);
				fd = -1;
				continue;
			}

			break;
		}

		freeaddrinfo(info);

		if (fd < 0) {
			print_msg(JERS_LOG_WARNING, "Failed to connect to daemon host '%s:%d'", agent.daemon_host, agent.daemon_port);
			goto connect_fail;
		}
	} else {
		print_msg(JERS_LOG_INFO, "Attempting to connect locally to JERS daemon using UNIX socket");

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, agent.daemon_socket_path, sizeof(addr.sun_path)-1);

		if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
			print_msg(JERS_LOG_WARNING, "Failed to connect to JERS daemon via unix socket: %s", strerror(errno));
			goto connect_fail;
		}
	}

	struct epoll_event ee = {0};
	ee.events = EPOLLIN;
	ee.data.ptr = &agent;

	if (epoll_ctl(agent.event_fd, EPOLL_CTL_ADD, fd, &ee)) {
		print_msg(JERS_LOG_WARNING, "Failed to set epoll event on daemon socket: %s", strerror(errno));
		goto connect_fail;
	}

	agent.daemon_fd = fd;
	send_login();

	return 0;

connect_fail:
	if (fd >= 0)
		close(fd);

	sleep(5);
	return 1;
}

int parseOpts(int argc, char * argv[]) {
	for (int i = 0; i < argc; i++) {
		if (strcasecmp("--daemon", argv[i]) == 0)
			agent.daemon = 1;
	}

	return 0;
}

void disconnectFromDaemon(void) {
	struct epoll_event ee;

	print_msg(JERS_LOG_WARNING, "Disconnecting from daemon");

	if (epoll_ctl(agent.event_fd, EPOLL_CTL_DEL, agent.daemon_fd, &ee))
		print_msg(JERS_LOG_WARNING, "Failed to remove daemon socket from epoll");

	close(agent.daemon_fd);
	agent.daemon_fd = -1;
	agent.next_connect = time(NULL) + RECONNECT_WAIT;

	agent.requests.used = 0;
	agent.responses.used = 0;
	agent.responses_sent = 0;
}

void shutdownHandler(int signum) {
	(void) signum;
	shutdown_requested = 1;
}

void setupAdoptionSocket(void) {
	int fd = -1;
	struct sockaddr_un addr;
	char *socket_path = DEFAULT_JOB_SOCKET;

	print_msg(JERS_LOG_DEBUG, "Setting up job adoption socket: %s", socket_path);

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (fd < 0)
		error_die("Failed to create socket for job adoption: %s\n", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

	unlink(socket_path);

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
		error_die("Failed to bind socket for job adoption socket: %s\n", strerror(errno));

	if (listen(fd, 1024) == -1) 
		error_die("Failed to listen on socket for job adoption socket: %s\n", strerror(errno));

	if (chmod(socket_path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) != 0)
		error_die("chmod failed on job adoption socket %s", strerror(errno));

	/* Add it to our event fd and set it to readable */
	struct epoll_event ee;
	ee.events = EPOLLIN;
	ee.data.ptr = &agent.job_adopt;

	agent.job_adopt.socket = fd;
	agent.job_adopt.ptr = NULL;
	agent.job_adopt.events = 0;
	agent.job_adopt.type = JOB_ADOPT_CONN;
	agent.job_adopt.event_fd = agent.event_fd;

	if (epoll_ctl(agent.event_fd, EPOLL_CTL_ADD, fd, &ee))
		error_die("Failed to set epoll event on job adopt socket: %s", strerror(errno));

	return;
}

void setupProxySocket(const char *path) {
	int fd = -1;
	struct sockaddr_un addr;

	if (path == NULL)
		path = DEFAULT_PROXY_SOCKET;

	print_msg(JERS_LOG_DEBUG, "Setting up cmd proxy listening socket: %s", path);

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (fd < 0)
		error_die("Failed to create socket for proxy socket: %s\n", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
	
	unlink(path);

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
		error_die("Failed to bind socket for proxy socket: %s\n", strerror(errno));

	if (listen(fd, 1024) == -1) 
		error_die("Failed to listen on socket for proxy socket: %s\n", strerror(errno));

	if (chmod(path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) != 0)
		error_die("chmod failed on proxy socket %s", strerror(errno));

	/* Add it to our event fd and set it to readable */
	struct epoll_event ee;
	ee.events = EPOLLIN;
	ee.data.ptr = &agent.client_proxy;

	agent.client_proxy.socket = fd;
	agent.client_proxy.ptr = NULL;
	agent.client_proxy.events = 0;
	agent.client_proxy.type = CLIENT_PROXY_CONN;
	agent.client_proxy.event_fd = agent.event_fd;

	if (epoll_ctl(agent.event_fd, EPOLL_CTL_ADD, fd, &ee))
		error_die("Failed to set epoll event on proxy socket: %s", strerror(errno));

	return;
}

int send_proxy_connect(proxyClient *c) {
	buff_t connRequest;
	initRequest(&connRequest, AGENT_PROXY_CONN, 1);

	JSONAddInt(&connRequest, PID, c->pid);
	JSONAddInt(&connRequest, UID, c->uid);

	sendRequest(&connRequest);

	return 0;
}

int send_proxy_data(proxyClient *c, char *data, size_t size) {
	buff_t dataRequest;
	initRequest(&dataRequest, AGENT_PROXY_DATA, 1);

	JSONAddInt(&dataRequest, PID, c->pid);
	JSONAddStringN(&dataRequest, PROXYDATA, data, size);

	sendRequest(&dataRequest);
	return 0;
}

int send_proxy_close(proxyClient *c) {
	buff_t closeRequest;
	initRequest(&closeRequest, AGENT_PROXY_CLOSE, 1);

	JSONAddInt(&closeRequest, PID, c->pid);

	sendRequest(&closeRequest);
	return 0;
}

void proxy_cleanup(proxyClient *c) {
	if (agent.daemon_fd != -1)
		send_proxy_close(c);

	removeProxyClient(c);
	free(c);
	return;
}

void sendProxyError(proxyClient *c, int error, const char *msg) {
	buff_t error_resp;
	char error_message[128];
	const char * error_type = getErrType(error);

	snprintf(error_message, sizeof(error_message), "%s %s", error_type, msg ? msg : "");

	buffNew(&error_resp, 256);
	JSONStart(&error_resp);
	JSONAddString(&error_resp, ERROR, error_message);
	JSONEnd(&error_resp);

	buffAddBuff(&c->response, &error_resp);
	pollSetWritable(&c->connection);

	buffFree(&error_resp);
}

void check_proxy_clients(void) {
	/* Check our proxy clients for any messages to forward */
	proxyClient *c = proxyClientList;

	while (c) {
		proxyClient *next = c->next;

		if (c->clean_up) {
			proxy_cleanup(c);
			c = next;
			continue;
		}

		if (c->error) {
			c = next;
			continue;
		}

		if (c->connect_sent == 0) {
			/* Send the connect message if we haven't already
			 * If we aren't connected to the daemon, send the client an error now */

			if (agent.daemon_fd == -1 || agent.in_recon) {
				print_msg(JERS_LOG_WARNING, "Not connected to main daemon, return error to proxy request");
				sendProxyError(c, JERS_ERR_NOTCONN, "Agent not connected");
				c->error = 1;
				c = next;
				continue;
			}
print_msg(JERS_LOG_DEBUG, "Sending connect for proxy client");
			if (send_proxy_connect(c) != 0) {
				handleClientProxyDisconnect(c);
				proxy_cleanup(c);
				c = next;
				continue;
			}

			c->connect_sent = 1;
		}

		if (c->request_forwarded < c->request.used) {
			/* Forward more data to the main daemon */
			send_proxy_data(c, c->request.data + c->request_forwarded, c->request.used - c->request_forwarded);
			c->request.used = c->request_forwarded = 0;
		}

		c = next;
	}
}

void handleReadable(struct epoll_event *e) {
	struct connectionType * connection = e->data.ptr;
	int status = 0;

	switch (connection->type) {
		case CLIENT_PROXY_CONN: status = handleClientProxyConnection(connection); break;
		case CLIENT_PROXY:      status = handleClientProxyRead(connection->ptr); break;
		case JOB_ADOPT_CONN:    status = handleJobAdoptConnection(connection); break;
		case JOB_ADOPT:         status = handleJobAdoptRead(connection->ptr); break;
		default:                print_msg(JERS_LOG_WARNING, "Unexpected read event - Ignoring"); break;
	}

	if (status) {
		/* Disconnected */
		e->data.ptr = NULL;
	}

	return;
}

void handleWritable(struct epoll_event *e) {
	struct connectionType * connection = e->data.ptr;

	/* Disconnected before handling the write event */
	if (connection == NULL)
		return;

	switch (connection->type) {
		case CLIENT_PROXY: handleClientProxyWrite(connection->ptr); break;
		default:           print_msg(JERS_LOG_WARNING, "Unexpected write event - Ignoring"); break;
	}

	return;
}

/* Scan /proc for orphaned jobs to adopt */

void adoptionScan(void) {
	glob_t globbuff;
	int rc;
	char *line = NULL;
	size_t line_size = 0;

	print_msg_info("Starting adoption scan...");

	if ( (rc = glob("/proc/*/cmdline", 0, NULL, &globbuff)) != 0) {
		if (rc == GLOB_NOMATCH)
			return;

		error_die("Failed to glob /proc/*/cmdline: %s\n",
			rc == GLOB_ABORTED ? "Glob aborted" : rc == GLOB_NOSPACE ? "Glob nospace" : "Unknown glob error");
	}

	for (size_t i = 0; i < globbuff.gl_pathc; i++) {
		FILE *f = fopen(globbuff.gl_pathv[i], "r");

		if (f == NULL) {
			if (errno == ENOENT)
				continue;

			error_die("Failed to open '%s': %s", globbuff.gl_pathv[i], strerror(errno));
		}

		ssize_t len = getline(&line, &line_size, f);

		if (len <= 0) {
			if (errno == ENOENT) {
				fclose(f);
				continue;
			}

			error_die("Failed to read '%s': %s", globbuff.gl_pathv[i], strerror(errno));
		}

		fclose(f);

		if (strncmp(line, "jers_agentd[", 12) != 0)
			continue;

		pid_t pid ,jobpid;
		time_t start_time = 0;
		jobid_t jobid = atoi(line + 12);
		int attempts = 0;
		int done = 0;

		/* Extract the pid */
		char *p = strchr(globbuff.gl_pathv[i] + 1, '/');

		if (p == NULL)
			error_die("Didn't find the second '/' in '%s'", globbuff.gl_pathv[i]);

		pid = atoi(p + 1);

		/* Open the associated adopt file for the orphaned job */
		do {
			int rc = loadAdoptFile(jobid, &pid, &jobpid, &start_time);

			if (rc == 0) {
				done = 1;
				break;
			}

			attempts++;
			print_msg_info("Failed to adopt jobid %d (Attempt %d/5). Sleeping %d seconds", jobid, attempts, ADOPT_SLEEP);
			sleep(ADOPT_SLEEP);
		} while (attempts < 5);

		if (done == 0)
			error_die("Failed to adopt job. jobid:%u pid:%u - Failed to obtain details from adopt file", jobid, pid);

		print_msg_info("Adopted orphaned job: jobid:%u pid:%d jobpid:%d starttime:%ld", jobid, pid, jobpid, start_time);

		/* Add this job as a running job */
		addOrphanJob(jobid, pid, jobpid, start_time);
	}

	free(line);
	globfree(&globbuff);

	return;
}

int handleJobAdoptConnection(struct connectionType * connection) {
	int client_fd = _accept(connection->socket);

	if (client_fd < 0) {
		print_msg(JERS_LOG_INFO, "accept() failed during job adopt connect: %s", strerror(errno));
		return 1;
	}

	/* Get the PID off the socket to match to the job */
	struct ucred creds;
	socklen_t len = sizeof(struct ucred);

	if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &creds, &len) == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to get peercred from job adopt connection: %s", strerror(errno));
		print_msg(JERS_LOG_WARNING, "Closing connection to job adopt");
		close(client_fd);
		return 1;
	}

	/* Locate the corrisponding 'running_job' entry */
	struct runningJob *j = agent.jobs;

	while (j && j->pid != creds.pid)
		j = j->next;

	if (j == NULL) {
		print_msg_warning("Job adoption attempt from unknown PID");
		close(client_fd);
		return 1;
	}

	j->connection.type = JOB_ADOPT;
	j->connection.ptr = j;
	j->connection.socket = client_fd;
	j->connection.event_fd = connection->event_fd;
	j->connection.events = 0;

	/* Add this client to our event polling */
	if (pollSetReadable(&j->connection) != 0) {
		print_msg(JERS_LOG_WARNING, "Failed to set job adoption as readable: %s", strerror(errno));
		close(client_fd);
		return 1;
	}

	return 0;
}

int handleJobAdoptRead(struct runningJob *j) {
	ssize_t len = _recv(j->connection.socket, &j->job_completion + j->completion_read, sizeof(struct jobCompletion) - j->completion_read);

	if (len < 0) {
		if ((errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;

		print_msg(JERS_LOG_WARNING, "failed to read from job adoption: %s\n", strerror(errno));
		close(j->connection.socket);
		return 1;
	} else if (len == 0) {
		/* Disconnected */
		close(j->connection.socket);
		return 1;
	}

	j->completion_read += len;

	if (j->completion_read == sizeof(struct jobCompletion)) {
		if (pollRemoveSocket(&j->connection) != 0)
			fprintf(stderr, "Failed to remove job adoption socket");

		close(j->connection.socket);
	}

	return 0;
}

int main (int argc, char * argv[]) {
	int i;

	memset(&agent, 0, sizeof(struct agent));
	parseOpts(argc, argv);

	if (agent.daemon)
		setLogfileName(server_log);

	loadConfig(NULL);

	setup_handlers(shutdownHandler);

#ifdef INIT_SETPROCTITLE_REPLACEMENT
	spt_init(argc, argv);
#endif

	print_msg(JERS_LOG_INFO, "\n"
			"     _  ___  ___  ___                   \n"
			"  _ | || __|| _ \\/ __|                  \n"
			" | || || _| |   /\\__ \\                  \n"
			"  \\__/ |___||_|_\\|___/  Version %d.%d.%d\n\n", JERS_MAJOR, JERS_MINOR, JERS_PATCH);

	print_msg(JERS_LOG_INFO, "jers_agentd v%d.%d.%d starting.", JERS_MAJOR, JERS_MINOR, JERS_PATCH);

	if (getuid() != 0)
		error_die("JERS_AGENTD is required to be run as root");

	agent.daemon_fd = -1;
	agent.client_proxy.socket = -1;

	/* Setup epoll on the socket */
	agent.event_fd = epoll_create1(EPOLL_CLOEXEC);

	struct epoll_event * events = malloc(sizeof(struct epoll_event) * MAX_EVENTS);

	/* Buffer to store our requests */
	buffNew(&agent.requests, 0x10000);

	/* Buffer to store our responses */
	buffNew(&agent.responses, 0);
	agent.responses_sent = 0;

	/* Sort the fields */
	sortfields();

	/* Attempt to adopt running jobs and setup the connection socket */
	adoptionScan();
	setupAdoptionSocket();

	/* Create a proxy listening unix socket */
	setupProxySocket(NULL);

	while(1) {
		if (shutdown_requested) {
			print_msg(JERS_LOG_INFO, "Shutdown has been requested");
			break;
		}

		if (agent.daemon_fd == -1) {
			if (connectjers() == 0)
				print_msg(JERS_LOG_INFO, "Connected to JERS daemon");
		}

		int status = epoll_wait(agent.event_fd, events, MAX_EVENTS, 10);

		for (i = 0; i < status; i++) {
			struct epoll_event * e = &events[i];
			if (e->data.ptr == &agent) {
				/* Event for the agent socket */
				if (e->events & EPOLLIN) {
					/* Message/s from the main daemon.
					* We simply keep appending to our request buffer here */

					buffResize(&agent.requests, 0x10000);

					int len = _recv(agent.daemon_fd, agent.requests.data + agent.requests.used, agent.requests.size - agent.requests.used);

					if (len <= 0) {
						/* lost connection to the main daemon. */
						// Keep going, but try and reconnect every 10 seconds.
						// If we reconnect, the daemon should send a reconciliation message to us
						// and we will send back the details of every job we have in memory.
						print_msg(JERS_LOG_WARNING, "Got disconnected from JERS daemon. Will try to reconnect");
						disconnectFromDaemon();
						break;
					} else {
						agent.requests.used += len;
					}
				}

				if (e->events & EPOLLOUT) {
					/* We can write to the main daemon */
					if (agent.responses.used - agent.responses_sent <= 0)
						continue;

					int len = _send(agent.daemon_fd, agent.responses.data + agent.responses_sent, agent.responses.used - agent.responses_sent);

					if (len <= 0) {
						/* lost connection to the main daemon. */
						// Keep going, but try and reconnect every 10 seconds.
						// If we reconnect, the daemon should send a reconciliation message to us
						// and we will send back the details of every job we have in memory.
						print_msg(JERS_LOG_WARNING, "Got disconnected from JERS daemon (during send). Will try to reconnect");
						disconnectFromDaemon();
						break;
					}

					agent.responses_sent += len;

					if (agent.responses_sent == agent.responses.used) {
						struct epoll_event ee;
						ee.events = EPOLLIN;
						ee.data.ptr = &agent;
						if (epoll_ctl(agent.event_fd, EPOLL_CTL_MOD, agent.daemon_fd, &ee)) {
							print_msg(JERS_LOG_WARNING, "Failed to clear EPOLLOUT from daemon socket");
						}

						agent.responses_sent = agent.responses.used = 0;
					}
				}
			} else {
				/* If not on the daemon proxy, the ptr should be to a connectionType structure */
				if (e->events & EPOLLIN)
					handleReadable(e);

				if (e->events & EPOLLOUT)
					handleWritable(e);
			}
		}

		process_messages();
		check_children();
		check_proxy_clients();
	}

	print_msg(JERS_LOG_INFO, "Finished.");

	return 0;
}
