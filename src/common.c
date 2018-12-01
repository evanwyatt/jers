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
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

/* Return the time, in milliseconds
 * Note: This is not the real time, and is mainly used
 *       for timed events, where only the duration between
 *       calls is important.  */

long getTimeMS(void) {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
	return (tp.tv_sec * 1000) + (tp.tv_nsec / 1000000);
}

/* Return a formated timespec as a static string
 *  - If elapsed is zero, print as year-month-day hour:minute:second.milliseconds
 *    Otherwise print an elapsed time as minutes seconds milliseconds
 *    ie 1969-07-21 02:56:15.000
 *    or 151m 40.000s */

char * print_time(struct timespec * time, int elapsed) {
	static char formatted[64];

	if (elapsed) {
		int minutes, seconds;

		minutes = time->tv_sec / 60;
		seconds = time->tv_sec % 60;

		snprintf(formatted, sizeof(formatted), "%dm %d.%03lds", minutes, seconds, time->tv_nsec / 1000000);

	} else {
		struct tm * tm = localtime(&time->tv_sec);
		size_t len = strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", tm);
		len += snprintf(formatted + len, sizeof(formatted) - len, ".%03ld", time->tv_nsec / 1000000);
		strftime(formatted + len, sizeof(formatted) - len, " %Z", tm);
	}

	return formatted;
}

/* Return the difference between 2 timespec structures */

void timespec_diff(const struct timespec *start, const struct timespec *end, struct timespec *diff) {
	diff->tv_sec = 0;
	diff->tv_nsec = 0;

	if (end->tv_nsec < start->tv_nsec) {
		diff->tv_sec = end->tv_sec - start->tv_sec - 1;
		diff->tv_nsec = end->tv_nsec - start->tv_nsec + 1000000000ul;
	} else {
		diff->tv_sec = end->tv_sec - start->tv_sec;
		diff->tv_nsec = end->tv_nsec - start->tv_nsec;
	}
}

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

/* Remove leading and trailing whitespace from a string */

char * removeWhitespace(char * str) {
	/* Trailing */
	int i;
	for (i = strlen(str) - 1; (i >= 0 && (str[i] == ' ' || str[i] == '\t')); i--);

	str[i + 1] = '\0';

	/* Leading */
	i = 0;
	while (str[i] && (str[i] == ' ' || str[i] == '\t'))
		i++;

	if (str[i])
		memmove(str, &str[i], strlen(str) - i + 1);

	return str;
}

char * gethost(void) {
	static char host[1024] = "";

	if (*host)
		return host;

	gethostname(host, sizeof(host));
	host[sizeof(host) -1] = '\0';
	return host;
}

void uppercasestring(char * str) {
	char * ptr = str;

	while (*ptr) {
		*ptr = toupper(*ptr);
		ptr++;
	}
}

void lowercasestring(char * str) {
	char * ptr = str;

	while (*ptr) {
		*ptr = tolower(*ptr);
		ptr++;
	}
}
