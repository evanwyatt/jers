#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "logging.h"

#define SLOWLOG_FILENAME "/var/log/jers/slow_requests.log"

extern char * server_log;
extern int server_log_mode;
extern volatile sig_atomic_t reopen_logfile;
extern volatile sig_atomic_t reopen_slowlog;

char * logfile_name = NULL;

static void _logMessage(const char * whom, int level, const char * message) {
	const char * levels[] = {"DEBUG", "INFO", "WARNING", "CRITICAL"};
	char currentTime[64];
	struct timespec tp;
	struct tm * tm;
	time_t t;

	if (logfile_name && reopen_logfile) {
		reopen_logfile = 0;
		fprintf(stdout, "Rotating logfile\n");
		fflush(stdout);

		openDaemonLog(logfile_name);
	}

	clock_gettime(CLOCK_REALTIME_COARSE, &tp);
	t = tp.tv_sec;
	tm = localtime(&t);

	strftime(currentTime, sizeof(currentTime), "%d %b %H:%M:%S", tm);
	fprintf(stdout, "%d-%s %s.%03d [%8s] %s", (int)getpid(), whom, currentTime, (int)tp.tv_nsec/1000000, levels[level], message);

	if (message[strlen(message)-1] != '\n')
		fputc('\n', stdout);

	fflush(stdout);
}

void error_die(char * msg, ...) {
	va_list args;
	char logMessage[1024];

	va_start(args, msg);
	vsnprintf(logMessage, sizeof(logMessage), msg, args);
	va_end(args);

	_logMessage(server_log, JERS_LOG_CRITICAL, logMessage);
	_logMessage(server_log, JERS_LOG_CRITICAL, "**** Server fatal error - exiting ****");
	fflush(stdout);
	fflush(stderr);

	exit(EXIT_FAILURE);
}

void setLogfileName(char * name) {
	logfile_name = name;
}

void print_msg(int level, const char * format, ...) {
	va_list args;
	char logMessage[1024];

	if (level < server_log_mode)
		return;

	va_start(args, format);
	vsnprintf(logMessage, sizeof(logMessage), format, args);
	va_end(args);

	_logMessage(server_log, level, logMessage);
}

void openDaemonLog(char * name) {
	int new_logging_fd;
	static char * new_log = NULL;

	if (name == NULL)
		return;

	if (new_log == NULL)
		asprintf(&new_log, "%s/%s.log", "/var/log/jers/", name);

	new_logging_fd = open(new_log, O_APPEND|O_CREAT|O_WRONLY, 0644);

	if (new_logging_fd < 0) {
		print_msg(JERS_LOG_WARNING, "Failed to open new logfile '%s':%s", new_log, strerror(errno));
		print_msg(JERS_LOG_WARNING, "Using existing logfile");
		return;
	}

	/* Redirect stdout/stderr to this logfile */
	dup2(new_logging_fd, STDOUT_FILENO);
	dup2(new_logging_fd, STDERR_FILENO);
	close(new_logging_fd);

	return;
}

void logSlowRequest(const char *cmd, uid_t uid, int64_t duration, const char *request) {
	static int log_fd = -1;
	char *logname = SLOWLOG_FILENAME;
	struct timespec now;

	clock_gettime(CLOCK_REALTIME_COARSE, &now);

	if (log_fd == -1 || reopen_slowlog) {
		if (log_fd != -1) {
			fdatasync(log_fd);
			close(log_fd);
			reopen_slowlog = 0;
		}

		log_fd = open(logname, O_APPEND|O_CREAT|O_WRONLY, 0644);

		if (log_fd < 0) {
			print_msg(JERS_LOG_WARNING, "Failed to open slowlog file '%s':%s", logname, strerror(errno));
			return;
		}

		dprintf(log_fd, "#TIME\tUID\tDURATION\tCMD\tREQUEST\n");
	}

	dprintf(log_fd, "%ld.%03d\t%d\t%ld\t%s\t%s\n", now.tv_sec, (int)(now.tv_nsec / 1000000), uid, duration, cmd, request);

	return;
}