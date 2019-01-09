#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

extern char * server_log;
extern int server_log_mode;

void _logMessage(const char * whom, int level, const char * message) {
	const char * levels[] = {"DEBUG", "INFO", "WARNING", "CRITICAL"};
	char currentTime[64];
	struct timespec tp;
	struct tm * tm;
	time_t t;
	int len;

	clock_gettime(CLOCK_REALTIME_COARSE, &tp);
	t = tp.tv_sec;
	tm = localtime(&t);

	len = strftime(currentTime, sizeof(currentTime), "%d %b %H:%M:%S", tm);
	snprintf(currentTime + len, sizeof(currentTime) - len, ".%02d", (int)tp.tv_nsec/10000000);

	fprintf(stdout, "%d-%s %s [%8s] %s", (int)getpid(), whom, currentTime, levels[level], message);

	if (message[strlen(message)-1] != '\n')
		fputc('\n', stdout);
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
