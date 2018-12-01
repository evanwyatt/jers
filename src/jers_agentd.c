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
#define _GNU_SOURCE
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
#include <pwd.h>

#include "jers.h"
#include "common.h"
#include "resp.h"
#include "buffer.h"
#include "fields.h"

#define MAX_EVENTS 1024
#define INITIAL_SIZE 0x1000
#define REQUESTS_THRESHOLD 0x010000

#define JERS_DEFAULT_SHELL "/bin/bash"

int shutdownRequested = 0;
int loggingMode = JERS_LOG_DEBUG;

struct jobCompletion {
	int exitcode;
	struct rusage rusage;
};

/* Linked list of active jobs
 *  * Note: the pid refers to the pid of the jersJob program. */

struct runningJob {
	pid_t pid;
	jobid_t jobID;
	time_t startTime;
	time_t endTime;
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

	int daemon_fd;
	int event_fd;

	struct runningJob * jobs;
};

struct jersJobSpawn {
	jobid_t jobid;

	char * name;
	char * queue;
	char * shell;
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
};

struct agent agent;

void print_msg(int level, const char * format, ...) {
	va_list args;
	char logMessage[1024];

	if (level < loggingMode)
		return;

	va_start(args, format);
	vsnprintf(logMessage, sizeof(logMessage), format, args);
	va_end(args);

	_logMessage("jers_agent", level, logMessage);
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


char * createTempScript(struct jersJobSpawn * j) {
	static char script[1024];
	int i;
	sprintf(script, "/var/spool/jers/jers_%d", j->jobid);

	int fd = open (script, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IXUSR);

	if (fd < 0) {
		fprintf(stderr, "Failed to create temporary script: %s\n", strerror(errno));
		return NULL;
	}

	dprintf(fd, "#!%s\nexport JERS_JOBID=%d\nexport JERS_QUEUE='%s'\nexport JERS_JOBNAME='%s'\n", j->shell, j->jobid, j->queue, j->name);

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

void jersRunJob(struct jersJobSpawn * j, int socket) {
	pid_t jobPid;
	char * tempScript = NULL;
	int stdout_fd, stderr_fd;
	struct timespec start, end, elapsed;
	struct passwd * pw = NULL;

	/* Generate the temporary script we are going to execute */

	if ((tempScript = createTempScript(j)) == NULL) {
		_exit(1);
	}

	/* Drop our privs now, to prevent us from overwriting log files
	 * we shouldn't have access to. Also double check we aren't running a job as root */

	if (j->uid == 0) {
		fprintf(stderr, "Jobs running as root are NOT permitted.\n");
		_exit(1);
	}

	pw = getpwuid(j->uid);

	if (pw == NULL) {
		fprintf(stderr, "Failed to get details of uid %d: %s\n", j->uid, strerror(errno));
		_exit(1);
	}

	setuid(j->uid);
	setgid(j->uid);

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

	if (chdir(pw->pw_dir) != 0) {
		fprintf(stderr, "Failed to change directory to %s: %s\n", pw->pw_dir, strerror(errno));
		_exit(1);
	}

	clock_gettime(CLOCK_REALTIME_COARSE, &start);

	/* Fork & exec the job*/
	jobPid = fork();

	if (jobPid < 0) {
		fprintf(stderr, "Fork failed: %s\n", strerror(errno));
		_exit(1);
	}

	if (jobPid == 0) {
		setsid();

		if (j->nice)
			nice(j->nice);

		/* Environment */
		if (j->env_count) {
			int i;
			for(i = 0; i < j->env_count; i++) {
				char * ptr = strchr(j->envs[i], '=');
				if (ptr) {
					*ptr = 0;
					setenv(j->envs[i], ptr + 1, 1);
				} else {
					unsetenv(j->envs[i]);
				}
			}
		}

		/* Add our variables */

		char temp[64];
		sprintf(temp, "%d", j->jobid);
		setenv("JERS_JOBID", temp, 1);
		setenv("JERS_QUEUE", j->queue, 1);
		setenv("JERS_JOBNAME", j->name, 1);

		if (!j->shell) {
			j->shell = JERS_DEFAULT_SHELL;
		}

		dup2(stdin_fd, STDIN_FILENO);
		dup2(stdout_fd, STDOUT_FILENO);
		dup2(stderr_fd, STDERR_FILENO);

		close(stdout_fd);
		close(stderr_fd);

		int k = 0;
		char * argv[3];

		argv[k++] = j->shell;
		argv[k++] = tempScript;
		argv[k++] = NULL;

		execv(argv[0], argv);
		perror("execv failed for child");
		/* Will only get here if something goes horribly wrong with execv().*/
		_exit(1);
	}

	struct jobCompletion job_completion = {0};
	int rc = 0, sig = 0;

	/* Wait for the job to complete */
	while (wait4(jobPid, &job_completion.exitcode, 0, &job_completion.rusage) < 0) {
		if (errno == EINTR)
			continue;

		fprintf(stderr, "wait4() failed? %s\n", strerror(errno));
		_exit(1);
	}

	clock_gettime(CLOCK_REALTIME_COARSE, &end);
	timespec_diff(&start, &end, &elapsed);

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
	

	/* Write a summary to the log file and flush it */
	dprintf(stdout_fd, "========= Job Summary =========\n");
	dprintf(stdout_fd, " Elapsed Time : %s\n", print_time(&elapsed, 1));
	dprintf(stdout_fd, " Start time   : %s\n", print_time(&start, 0));
	dprintf(stdout_fd, " End time     : %s\n", print_time(&end, 0));
	dprintf(stdout_fd, " User CPU     : %s\n", print_time(&user_cpu, 1));
	dprintf(stdout_fd, " Sys CPU      : %s\n", print_time(&sys_cpu, 1));
	dprintf(stdout_fd, " Max RSS      : %ldKB\n", job_completion.rusage.ru_maxrss);
	dprintf(stdout_fd, " Exit Code    : %d\n", rc);

	if (sig)
		dprintf(stdout_fd, " Signal      : %d\n", sig);

	fdatasync(stdout_fd);
	close(stdout_fd);
	
	if (stderr_fd != stdout_fd)
		close(stderr_fd);

	int status = 0;

	do {
		status = write(socket, &job_completion, sizeof(struct jobCompletion));
		
		if (status != sizeof(struct jobCompletion))
			fprintf(stderr, "Failed to send completion to agent: (status=%d) %s\n", status, strerror(errno));
	} while (status == -1 && errno == EINTR);

	print_msg(JERS_LOG_DEBUG, "Job: %d finished: %d\n", j->jobid, job_completion.exitcode);

	_exit(0);
}



void removeJob (struct runningJob * j) {

	if (j->prev)
		j->prev->next = j->next;

	if (j->next)
		j->next->prev = j->prev;

	if (agent.jobs == j)
		agent.jobs = j->next;

}

struct runningJob * addJob (jobid_t jobid, pid_t pid, int socket) {
	struct runningJob * j = calloc(sizeof(struct runningJob), 1);
	j->startTime = time(NULL);
	j->pid = pid;
	j->jobID = jobid;
	j->socket = socket;

	if (agent.jobs) {
		j->next = agent.jobs;
		agent.jobs->prev = j;
	}

	agent.jobs = j;

	return j;
}

void send_msg(resp_t * r) {
	struct epoll_event ee;
	char * buffer;
	size_t length;
	int add_epoll = 0;

	if (agent.responses_sent <= agent.responses.used)
		add_epoll = 1;


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
	resp_t * r = respNew();
	print_msg(JERS_LOG_INFO, "Job started: JOBID:%d PID:%d", j->jobID, j->pid);

	respAddArray(r);
	respAddSimpleString(r, "JOB_STARTED");
	respAddInt(r, 1);
	respAddMap(r);
	addIntField(r, JOBID, j->jobID);
	addIntField(r, JOBPID, j->pid);
	respCloseMap(r);
	respCloseArray(r);

	send_msg(r);

	return 0;
}

int send_completion(struct runningJob * j) {
	resp_t * r = respNew();

	print_msg(JERS_LOG_INFO, "Job complete: JOBID:%d PID:%d RC:%d\n", j->jobID, j->pid, j->job_completion.exitcode);

	respAddArray(r);
	respAddSimpleString(r, "JOB_COMPLETED");
	respAddInt(r, 1);
	respAddMap(r);
	addIntField(r, JOBID, j->jobID);
	addIntField(r, EXITCODE, j->job_completion.exitcode);

	/* Usage info */
	//respAddInt(r, j->rusage.ru_utime.tv_sec);
	//respAddInt(r, j->rusage.ru_utime.tv_usec);
	//respAddInt(r, j->rusage.ru_stime.tv_sec);
	//respAddInt(r, j->rusage.ru_stime.tv_usec);

	respCloseMap(r);
	respCloseArray(r);

	send_msg(r);

	close(j->socket);

	removeJob(j);
	free(j);
	return 0;
}

/* Sent upon connection to confirm who we are */

int send_login(void) {
	resp_t * r = respNew();
	char host[256];

	gethostname(host, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';

	respAddArray(r);
	respAddSimpleString(r, "AGENT_LOGIN");
	respAddInt(r, 1);
	respAddMap(r);
	addStringField(r, NODE, host);
	respCloseMap(r);
	respCloseArray(r);

	print_msg(JERS_LOG_INFO, "Sending login");
	send_msg(r);
	return 0;
}

void handle_terminations(void) {
	pid_t pid;
	int child_status;
	int status;

	while ((pid = waitpid(-1, &child_status, WNOHANG)) > 0) {
		struct runningJob * j = agent.jobs;

		/* Find the jobid for this pid */
		while (j && j->pid != pid) {
			j = j->next;
		}

		if (!j) {
			fprintf(stderr, "Got completion for PID %d, but can't find a matching job?!\n", pid);
			continue;
		}

		/* If a job was started correctly, the child will always return 0
		 * Anything else indicates it failed to start. */

		if (child_status) {
			fprintf(stderr, "Job %d failed to start.\n", j->jobID);
			j->job_completion.exitcode = 255;
			send_completion(j);
			continue;
		}

		/* Read in the completion details */
		do {
			status = read(j->socket, &j->job_completion, sizeof(struct jobCompletion));
		} while (status == -1 && errno == EINTR);

		send_completion(j);
	}

	if (pid == -1 && errno != ECHILD) {
		fprintf(stderr, "waitpid failed? %s\n", strerror(errno));
	}
}

struct runningJob * spawn_job(struct jersJobSpawn * j) {

	/* Setup a socketpair so the child can return the usage infomation */
	int sockpair[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) != 0) {
		fprintf(stderr, "Failed to create socketpair: %s\n", strerror(errno));
		return NULL;
	}

	pid_t pid = fork();

	if (pid == -1) {
		fprintf(stderr, "FAILED TO FORK(): %s\n", strerror(errno));
		return NULL;
	} else if (pid == 0) {
		/* Child */
		close(sockpair[0]);

		jersRunJob(j, sockpair[1]);
		/* jersRunJob() does not return */
	}

	close(sockpair[1]);

	return addJob(j->jobid, pid, sockpair[0]);
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

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
		}

	}

	if ((started = spawn_job(&j)) != NULL) {
		send_start(started);
	} else {
		// Send start failed message
		print_msg(JERS_LOG_WARNING, "JOBID %d failed to start", j.jobid);
	}

	free(j.name);
	free(j.queue);
	free(j.shell);
	free(j.pre_command);
	free(j.argv);
	free(j.envs);

	return;
}

void stop_command(msg_t * m) {
	printf("Stop_command:\n");
}

void recon_command(msg_t * m) {
	struct runningJob * j = NULL;

	/* The master daemon is requesting a list of all the jobs we have in memory */

	printf("=== Start Recon ===\n");

	for (j = agent.jobs; j != NULL; j = j->next) {
		printf("JobID: %d PID:%d ExitCode: %d\n", j->jobID, j->pid, j->job_completion.exitcode);
	}

	printf("=== End Recon ===\n");
}

/* Process a command */
void process_message(msg_t * m) {

	print_msg(JERS_LOG_DEBUG, "Got command '%s'", m->command);

	if (strcmp(m->command, "START_JOB") == 0) {
		/* Start a job */
		start_command(m);
	} else if (strcmp(m->command, "STOP_JOB") == 0) {
		/* Stop a job */
		stop_command(m);
	} else if (strcmp(m->command, "RECON") == 0) {
		recon_command(m);
		/* The JERS Daemon is requesting a list of job statuses */
	} else {
		print_msg(JERS_LOG_WARNING, "Got an unexpected command message '%s'", m->command);
	}

	free_message(m, &agent.requests);
}


/* Loop through processing messages. */
void process_messages(void) {
	while (load_message(&agent.msg, &agent.requests) == 0) {
		process_message(&agent.msg);
	}
}

int connectjers(void) {
	struct sockaddr_un addr;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "/var/run/jers/agent.socket", sizeof(addr.sun_path)-1); //TODO: remove hardcoded path

	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to connect to JERS daemon: %s", strerror(errno));
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

int main (int argc, char * argv[]) {

	int i;

	/* Connect to the main daemon */
	print_msg(JERS_LOG_INFO, "Starting JERS_AGENT");

	memset(&agent, 0, sizeof(struct agent));

	agent.daemon_fd = -1;

	/* Setup epoll on the socket */
	agent.event_fd = epoll_create(MAX_EVENTS);

	struct epoll_event * events = malloc(sizeof(struct epoll_event) * MAX_EVENTS);

	/* Buffer to store our requests */
	buffNew(&agent.requests, 0);

	/* Buffer to store our responses */
	buffNew(&agent.responses, 0);
	agent.responses_sent = 0;

	while(1) {

		if (shutdownRequested)
			break;

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
				buffResize(&agent.requests, 0);

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
			} else {
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

				}

			}
		}
			
		process_messages();

		handle_terminations();
	}

	print_msg(JERS_LOG_INFO, "Finished.");

	return 0;
}
