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
#include <grp.h>
#include <sys/prctl.h>

#include "jers.h"
#include "logging.h"
#include "common.h"
#include "resp.h"
#include "buffer.h"
#include "fields.h"

#define MAX_EVENTS 1024
#define INITIAL_SIZE 0x1000
#define REQUESTS_THRESHOLD 0x010000
#define DEFAULT_DIR "/tmp"

int initMessage(resp_t * r, const char * resp_name, int version);
void error_die(char *, ...);

char * server_log = "jers_agentd";
int server_log_mode = JERS_LOG_DEBUG;

volatile sig_atomic_t shutdown_requested = 0;

struct jobCompletion {
	int exitcode;
	time_t finish_time;
	struct rusage rusage;
};

/* Linked list of active jobs
 *  * Note: the pid refers to the pid of the jersJob program. */

struct runningJob {
	pid_t pid;
	pid_t job_pid;
	jobid_t jobID;
	time_t start_time;
	int kill;
	int socket;
	struct jobCompletion job_completion;

	struct runningJob * next;
	struct runningJob * prev;
};

struct agent {
	buff_t requests;
	buff_t responses;
	size_t responses_sent;

	msg_t msg;

	int daemon;

	int daemon_fd;
	int event_fd;

	struct runningJob * jobs;
	int64_t running_jobs;
};

struct jersJobSpawn {
	jobid_t jobid;

	char * name;
	char * queue;
	char * shell;

	char * wrapper;
	char * pre_command;
	char * post_command;

	uid_t uid;
	int nice;

	int64_t argc;
	char ** argv;

	int64_t env_count;
	char ** envs;

	char * stdout;
	char * stderr;

	struct user * u;
};

struct agent agent;

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


char * createTempScript(struct jersJobSpawn * j) {
	static char script[1024];
	int i;
	sprintf(script, "/var/spool/jers/jers_%d", j->jobid);

	int fd = open (script, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IXUSR);

	if (fd < 0) {
		fprintf(stderr, "Failed to create temporary script: %s\n", strerror(errno));
		return NULL;
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
		unlink(script);
		return NULL;
	}

	close(fd);
	return script;
}

/* jersRunJob() is essentially a wrapper around a users job to setup the environment,
 * capture rusage infomation, handle signals, etc.
 *
 * The status/usage infomation is passed back through to the jersagentd via a socket
 *
 * Note: This function does not return */

void jersRunJob(struct jersJobSpawn * j, struct timespec * start, int socket) {
	pid_t jobPid;
	char * tempScript = NULL;
	int stdout_fd, stderr_fd;
	struct timespec end, elapsed;
	int i;

	/* Generate the temporary script we are going to execute, if no wrapper was specified. */
	if (j->wrapper == NULL) {
		if ((tempScript = createTempScript(j)) == NULL) {
			_exit(1);
		}
	}

	/* Drop our privs now, to prevent us from overwriting log files
	 * we shouldn't have access to. Also double check we aren't running a job as root */

	if (j->u->uid == 0) {
		fprintf(stderr, "Jobs running as root are NOT permitted.\n");
		_exit(1);
	}

	if (setgroups(j->u->group_count, j->u->group_list) != 0) {
		fprintf(stderr, "Failed to initalise groups for job %d: %s\n", j->jobid, strerror(errno));
		_exit(1);
	}

	if (setgid(j->u->gid) != 0) {
		perror("setgid");
		_exit(1);
	}

	if (setuid(j->u->uid) != 0) {
		perror("setuid");
		_exit(1);
	}

	if (getuid() == 0) {
		fprintf(stderr, "Job STILL running as root after dropping privs\n");
		_exit(1);
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
		_exit(1);
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
		_exit(1);
	}

	/* Open STDIN as /dev/null */
	int stdin_fd = open("/dev/null", O_RDONLY);

	if (stdin_fd < 0) {
		fprintf(stderr, "Failed to open /dev/null for stdin: %s\n", strerror(errno));
		_exit(1);
	}

	/* Start off in the users home directory */

	if (chdir(j->u->home_dir) != 0) {
		dprintf(stderr_fd, "Warning: Failed to change directory to %s: %s\n", j->u->home_dir, strerror(errno));
		dprintf(stderr_fd, "Attempting to use %s\n", DEFAULT_DIR);

		if (chdir(DEFAULT_DIR) != 0) {
			dprintf(stderr_fd, "Error: Failed to change directory to %s: %s\n", DEFAULT_DIR, strerror(errno));
			_exit(1);
		}
	}

	char new_proc_name[16];
	snprintf(new_proc_name, sizeof(new_proc_name), "jers_%d", j->jobid);

	if (prctl(PR_SET_NAME, new_proc_name, NULL, NULL, NULL) != 0)
		dprintf(stderr_fd, "Warning: Failed to set process name for new job %d: %s\n", j->jobid, strerror(errno));

	/* Fork & exec the job */
	jobPid = fork();

	if (jobPid < 0) {
		fprintf(stderr, "Fork failed: %s\n", strerror(errno));
		_exit(1);
	}

	if (jobPid == 0) {
		setsid();

		if (j->nice)
			nice(j->nice);

		/* Add our variables into the environment, along with
		 * any the user has requested.
		 * - We can modify the cached version directly, as we
		 *   are in a forked child.  */

		/* Resize if needed */
		int to_add = 6 + j->env_count;

		if (to_add >= j->u->env_size - j->u->env_count) {
			j->u->users_env = realloc(j->u->users_env, sizeof(char *) * (j->u->env_count + to_add + 1));
		}

		/* Add the users variables first */
		for (i = 0; i < j->env_count; i++) {
			j->u->users_env[j->u->env_count++] = j->envs[i];
		}

		/* Now our generic job ones */
		int len;
		len = asprintf(&j->u->users_env[j->u->env_count],"JERS_JOBID=%d ", j->jobid);
		j->u->users_env[j->u->env_count++][len-1] = '\0';

		len = asprintf(&j->u->users_env[j->u->env_count],"JERS_QUEUE=%s ", j->queue);
		j->u->users_env[j->u->env_count++][len-1] = '\0';

		len = asprintf(&j->u->users_env[j->u->env_count],"JERS_JOBNAME=%s ", j->name);
		j->u->users_env[j->u->env_count++][len-1] = '\0';

		len = asprintf(&j->u->users_env[j->u->env_count],"JERS_STDOUT=%s ", j->stdout ? j->stdout : "/dev/null");
		j->u->users_env[j->u->env_count++][len-1] = '\0';

		len = asprintf(&j->u->users_env[j->u->env_count],"JERS_STDERR=%s ", j->stderr ? j->stderr : j->stdout ? j->stdout : "/dev/null");
		j->u->users_env[j->u->env_count++][len-1] = '\0';

		j->u->users_env[j->u->env_count] = NULL;

		if (!j->shell) {
			j->shell = j->u->shell;
		}

		dup2(stdin_fd, STDIN_FILENO);
		dup2(stdout_fd, STDOUT_FILENO);
		dup2(stderr_fd, STDERR_FILENO);

		close(stdout_fd);
		close(stderr_fd);

		int k = 0;
		char * argv[j->argc + 3];

		argv[k++] = j->shell;

		if (j->wrapper) {
			int i;
			argv[k++] = j->wrapper;

			for (i = 0; i < j->argc; i++)
				argv[k++] = j->argv[i];
		}
		else {
			argv[k++] = tempScript;
		}

		argv[k++] = NULL;

		execvpe(argv[0], argv, j->u->users_env);
		perror("execv failed for child");
		/* Will only get here if something goes horribly wrong with execvpe().*/
		_exit(1);
	}

	struct jobCompletion job_completion = {0};
	int rc = 0, sig = 0, status;

	/* Send the PID of the job */
	do {
		status = write(socket, &jobPid, sizeof(jobPid));

		if (status != sizeof(jobPid))
			fprintf(stderr, "Failed to send PID of job %d to agent: (status=%d) %s\n", j->jobid, status, strerror(errno));
	} while (status == -1 && errno == EINTR);


	/* Wait for the job to complete */
	while (wait4(jobPid, &job_completion.exitcode, 0, &job_completion.rusage) < 0) {
		if (errno == EINTR)
			continue;

		fprintf(stderr, "wait4() failed pid:%d? %s\n", jobPid, strerror(errno));
		_exit(1);
	}

	clock_gettime(CLOCK_REALTIME_COARSE, &end);
	timespec_diff(start, &end, &elapsed);

	if (WIFEXITED(job_completion.exitcode)) {
		rc = WEXITSTATUS(job_completion.exitcode);
	} else if (WIFSIGNALED(job_completion.exitcode)) {
		sig = WTERMSIG(job_completion.exitcode);
		rc = 128 + sig;
	}

	struct timespec user_cpu, sys_cpu;
	user_cpu.tv_sec = job_completion.rusage.ru_utime.tv_sec;
	user_cpu.tv_nsec = job_completion.rusage.ru_utime.tv_usec * 1000;
	sys_cpu.tv_sec = job_completion.rusage.ru_stime.tv_sec;
	sys_cpu.tv_nsec = job_completion.rusage.ru_stime.tv_usec * 1000;

	job_completion.finish_time = end.tv_sec;	

	/* Write a summary to the log file and flush it to disk */
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
	dprintf(stdout_fd, " Exit Code    : %d\n", rc);

	if (sig)
		dprintf(stdout_fd, " Signal       : %d\n", sig);

	fdatasync(stdout_fd);
	close(stdout_fd);

	if (stderr_fd != stdout_fd)
		close(stderr_fd);

	do {
		status = write(socket, &job_completion, sizeof(struct jobCompletion));
		
		if (status != sizeof(struct jobCompletion))
			fprintf(stderr, "Failed to send completion to agent: (status=%d) %s\n", status, strerror(errno));
	} while (status == -1 && errno == EINTR);

	print_msg(JERS_LOG_DEBUG, "Job: %d finished: %d\n", j->jobid, job_completion.exitcode);
	close(socket);
	_exit(0);
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

struct runningJob * addJob (jobid_t jobid, pid_t pid, time_t start_time, int socket) {
	struct runningJob * j = calloc(sizeof(struct runningJob), 1);
	j->start_time = start_time;
	j->pid = pid;
	j->job_pid = 0;
	j->jobID = jobid;
	j->socket = socket;

	if (agent.jobs) {
		j->next = agent.jobs;
		agent.jobs->prev = j;
	}

	agent.jobs = j;

	agent.running_jobs++;
	return j;
}

void send_msg(resp_t * r) {
	struct epoll_event ee;
	char * buffer;
	size_t length;
	int add_epoll = 0;

	if (agent.responses_sent <= agent.responses.used)
		add_epoll = 1;


	respCloseArray(r);

	buffer = respFinish(r, &length);
	buffAdd(&agent.responses, buffer, length);

	free(buffer);

	/* Only set EPOLLOUT if we have didn't already have it set */
	if (!add_epoll)
		return;

	ee.events = EPOLLIN|EPOLLOUT;
	ee.data.ptr = NULL;

	if (epoll_ctl(agent.event_fd, EPOLL_CTL_MOD, agent.daemon_fd, &ee)) {
		print_msg(JERS_LOG_WARNING, "Failed to set epoll EPOLLOUT on daemon_fd");
	}

}
int send_start(struct runningJob * j) {
	resp_t r;
	print_msg(JERS_LOG_INFO, "Job started: JOBID:%d PID:%d", j->jobID, j->pid);

	initMessage(&r, "JOB_STARTED", 1);

	respAddMap(&r);
	addIntField(&r, JOBID, j->jobID);
	addIntField(&r, JOBPID, j->pid);
	addIntField(&r, STARTTIME, j->start_time);
	respCloseMap(&r);

	send_msg(&r);

	return 0;
}

int send_completion(struct runningJob * j) {
	resp_t r;

	/* Only send the completion if we are connected to the main daemon process */
	if (agent.daemon_fd == -1) {
		j->pid = -1;
		return 0;
	}

	initMessage(&r, "JOB_COMPLETED", 1);

	print_msg(JERS_LOG_INFO, "Job complete: JOBID:%d PID:%d RC:%d\n", j->jobID, j->pid, j->job_completion.exitcode);

	respAddMap(&r);
	addIntField(&r, JOBID, j->jobID);
	addIntField(&r, EXITCODE, j->job_completion.exitcode);
	addIntField(&r, FINISHTIME, j->job_completion.finish_time);

	/* Usage info */
	//TODO:
	//respAddInt(r, , j->rusage.ru_utime.tv_sec);
	//respAddInt(r, , j->rusage.ru_utime.tv_usec);
	//respAddInt(r, , j->rusage.ru_stime.tv_sec);
	//respAddInt(r, , j->rusage.ru_stime.tv_usec);
	//respAddint(r, , j->rusage.ru_maxrss);

	respCloseMap(&r);

	send_msg(&r);

	close(j->socket);

	removeJob(j);
	free(j);
	return 0;
}

/* Sent upon connection to confirm who we are */

int send_login(void) {
	resp_t r;
	char host[256];

	gethostname(host, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';

	initMessage(&r, "AGENT_LOGIN", 1);

	respAddMap(&r);
	addStringField(&r, NODE, host);
	respCloseMap(&r);

	print_msg(JERS_LOG_INFO, "Sending login");
	send_msg(&r);
	return 0;
}

int get_job_completion(struct runningJob * j) {
	int status;

	/* Get the PID if we haven't already. */
	if (j->job_pid == 0) {
		do {
			status = read(j->socket, &j->job_pid, sizeof(pid_t));
		} while (status == -1 && errno == EINTR);

		if (status == -1 && (errno != EAGAIN && errno != EWOULDBLOCK))
			fprintf(stderr, "Failed to read pid from job %d\n", j->jobID);
	}

	/* Read in the completion details */
	do {
		status = read(j->socket, &j->job_completion, sizeof(struct jobCompletion));
	} while (status == -1 && errno == EINTR);

	if (status == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/* Clear the pid for this job, we will try later to read in the completion infomation again later */
			j->pid = 0;
			return 1;
		} else {
			print_msg(JERS_LOG_WARNING, "Failed to read in job completion infomation for jobid:%d :%s", j->jobID, strerror(errno));
			j->job_completion.exitcode = 255; //TODO: Use a different exitcode for this?
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
		if (j->job_pid == 0) {	
			do {
				status = read(j->socket, &j->job_pid, sizeof(pid_t));
			} while (status == -1 && errno == EINTR);

			if (status == -1 && (errno != EAGAIN && errno != EWOULDBLOCK))
				fprintf(stderr, "Failed to read pid from job %d\n", j->jobID);

			if (j->job_pid && j->kill) {
				/* Send the saved signal to the job */
				print_msg(JERS_LOG_DEBUG, "Sending delayed signal to job:%d siugnum:%d\n", j->jobID, j->kill);

				if (kill(-j->job_pid, j->kill)) {
					print_msg(JERS_LOG_WARNING, "Failed to signal JOBID:%d with SIGNUM:%d", j->jobID, j->kill);
				}
			}
		}

		/* Check for any children we have cleaned up, but didn't get the completion infomation from */
		if (j->pid == 0) {
			struct runningJob * next = j->next;

			if (get_job_completion(j) == 0) {
				send_completion(j);
				j = next;
			} else {
				j = j->next;
			}
		} else {
			j = j->next;
		}
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

		/* If a job was started correctly, the child will always return 0
		 * - Anything else indicates it failed to start correctly */

		if (child_status) {
			fprintf(stderr, "Job %d failed to start.\n", j->jobID);
			j->job_completion.exitcode = 255; //TODO: Use a different exitcode for this.
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

struct runningJob * spawn_job(struct jersJobSpawn * j) {
	/* Setup a socketpair so the child can return the usage infomation */
	int sockpair[2];
	struct timespec start_time;

	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0, sockpair) != 0) {
		fprintf(stderr, "Failed to create socketpair: %s\n", strerror(errno));
		return NULL;
	}

	/* Need to flush stdout/stderr otherwise the child may get
	 * a copy of the buffered output from the parent */
	fflush(stdout);
	fflush(stderr);

	clock_gettime(CLOCK_REALTIME_COARSE, &start_time);

	pid_t pid = fork();

	if (pid == -1) {
		fprintf(stderr, "FAILED TO FORK(): %s\n", strerror(errno));
		return NULL;
	} else if (pid == 0) {
		/* Child */
		close(sockpair[0]);
		jersRunJob(j, &start_time, sockpair[1]);
		/* jersRunJob() does not return */
	}

	close(sockpair[1]);

	return addJob(j->jobid, pid, start_time.tv_sec, sockpair[0]);
}

void start_command(msg_t * m) {
	struct jersJobSpawn j = {0};
	struct runningJob * started = NULL;
	int i;

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

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
		}
	}

	/* Lookup the user from our cache */
	j.u = lookup_user(j.uid, 1);

	if (j.u == NULL)
		print_msg(JERS_LOG_WARNING, "Failed to find user for job:%d uid:%d\n", j.jobid, j.uid);

	if (j.u && (started = spawn_job(&j)) != NULL) {
		send_start(started);
	} else {
		// Send start failed message
		print_msg(JERS_LOG_WARNING, "JOBID %d failed to start", j.jobid);
		//TODO: Return an error 
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

	return;
}

void signal_command(msg_t * m) {
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
		return;
	}

	j = agent.jobs;
	while (j) {
		if (j->jobID == id)
			break;

		j = j->next;
	}

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got request to signal a job that is not running? %d", id);
		return;
	}

	if (j->job_pid == 0) {
		fprintf(stderr, "Got request to signal job %d, but it hasn't sent us its PID yet...\n", j->jobID);
		/* Set a flag so its killed as soon as we get the PID */
		j->kill = signum;
		return;
	}

	if (kill(-j->job_pid, signum) != 0)
		print_msg(JERS_LOG_WARNING, "Failed to signal JOBID:%d with SIGNUM:%d", id, signum);

	return;
}

void recon_command(msg_t * m) {
	struct runningJob * j = NULL;

	/* The master daemon is requesting a list of all the jobs we have in memory
	 * We can remove completed jobs here */

	printf("=== Start Recon ===\n");
	j = agent.jobs;

	while (j) {
		printf("JobID: %d PID:%d ExitCode: %d\n", j->jobID, j->pid, j->job_completion.exitcode);

		if (j->pid == -1) {
			struct runningJob * next = removeJob(j);
			free(j);
			j = next;
		}
		else {
			j = j->next;
		}
	}

	printf("=== End Recon ===\n");
}

/* Process a command */
void process_message(msg_t * m) {

	print_msg(JERS_LOG_DEBUG, "Got command '%s'", m->command);

	if (strcmp(m->command, "START_JOB") == 0) {
		start_command(m);
	} else if (strcmp(m->command, "SIG_JOB") == 0) {
		signal_command(m);
	} else if (strcmp(m->command, "RECON") == 0) {
		recon_command(m);
	} else {
		print_msg(JERS_LOG_WARNING, "Got an unexpected command message '%s'", m->command);
	}

	free_message(m, NULL);

	if (buffRemove(&agent.requests, m->reader.pos, 0x10000)) {
		m->reader.pos = 0;
		respReadUpdate(&m->reader, agent.requests.data, agent.requests.used);
	}
}

/* Loop through processing messages. */
void process_messages(void) {
	while (load_message(&agent.msg, &agent.requests) == 0) {
		process_message(&agent.msg);
	}
}

int connectjers(void) {
	struct sockaddr_un addr;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (fd < 0) {
		print_msg(JERS_LOG_WARNING, "Failed to connect to JERS daemon (socket failed): %s", strerror(errno));
		close(fd);
		sleep(5);
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "/run/jers/agent.sock", sizeof(addr.sun_path)-1); //TODO: remove hardcoded path

	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to connect to JERS daemon: %s", strerror(errno));
		close(fd);
		sleep(5);
		return 1;
	}

	struct epoll_event ee;
	ee.events = EPOLLIN;
	ee.data.ptr = NULL;

	if (epoll_ctl(agent.event_fd, EPOLL_CTL_ADD, fd, &ee)) {
		print_msg(JERS_LOG_WARNING, "Failed to set epoll event on daemon socket: %s", strerror(errno));
	}

	agent.daemon_fd = fd;

	send_login();

	return 0;
}

int parseOpts(int argc, char * argv[]) {
	for (int i = 0; i < argc; i++) {
		if (strcasecmp("--daemon", argv[i]) == 0)
			agent.daemon = 1;
	}

	return 0;
}

void shutdownHandler(int signum) {
	shutdown_requested = 1;
}

int main (int argc, char * argv[]) {
	int i;

	memset(&agent, 0, sizeof(struct agent));
	parseOpts(argc, argv);

	setup_handlers(shutdownHandler);

	if (agent.daemon)
		setLogfileName(server_log);

	/* Connect to the main daemon */
	print_msg(JERS_LOG_INFO, "Starting JERS_AGENTD");

	if (getuid() != 0)
		error_die("JERS_AGENTD is required to be run as root");

	agent.daemon_fd = -1;

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
			if (e->events & EPOLLIN) {
				/* Message/s from the main daemon.
				 * We simply keep appending to our request buffer here */

				buffResize(&agent.requests, 0x10000);

				int len = recv(agent.daemon_fd, agent.requests.data + agent.requests.used, agent.requests.size - agent.requests.used, 0);

				if (len <= 0) {
					/* lost connection to the main daemon. */
					// Keep going, but try and reconnect every 10 seconds.
					// If we reconnect, the daemon should send a reconciliation message to us
					// and we will send back the details of every job we have in memory.
					print_msg(JERS_LOG_WARNING, "Got disconnected from JERS daemon. Will try to reconnect");
					struct epoll_event ee;
					if (epoll_ctl(agent.event_fd, EPOLL_CTL_DEL, agent.daemon_fd, &ee)) {
						print_msg(JERS_LOG_WARNING, "Failed to remove daemon socket from epoll");
					}

					close(agent.daemon_fd);
					agent.daemon_fd = -1;
					break;
				} else {
					agent.requests.used += len;
				}
			}

			if (e->events & EPOLLOUT) {
				/* We can write to the main daemon */
				if (agent.responses.used - agent.responses_sent <= 0)
					continue;

				int len = send(agent.daemon_fd, agent.responses.data + agent.responses_sent, agent.responses.used - agent.responses_sent, 0);

				if (len <= 0) {
					/* lost connection to the main daemon. */
					// Keep going, but try and reconnect every 10 seconds.
					// If we reconnect, the daemon should send a reconciliation message to us
					// and we will send back the details of every job we have in memory.
					print_msg(JERS_LOG_WARNING, "Got disconnected from JERS daemon (during send). Will try to reconnect");
					struct epoll_event ee;
					if (epoll_ctl(agent.event_fd, EPOLL_CTL_DEL, agent.daemon_fd, &ee)) {
						print_msg(JERS_LOG_WARNING, "Failed to remove daemon socket from epoll");
					}

					close(agent.daemon_fd);
					agent.daemon_fd = -1;
					break;
				}

				agent.responses_sent += len;

				if (agent.responses_sent == agent.responses.used) {
					struct epoll_event ee;
					ee.events = EPOLLIN;
					ee.data.ptr = NULL;
					if (epoll_ctl(agent.event_fd, EPOLL_CTL_MOD, agent.daemon_fd, &ee)) {
                        print_msg(JERS_LOG_WARNING, "Failed to clear EPOLLOUT from daemon socket");
                    }

					agent.responses_sent = agent.responses.used = 0;
				}
			}
		}
			
		process_messages();
		check_children();
	}

	print_msg(JERS_LOG_INFO, "Finished.");

	return 0;
}
