/* Copyright (c) 2018 Evan Wyatt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *	this list of conditions and the following disclaimer.
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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <execinfo.h>
#include <signal.h>

#include <uthash.h>

#include "jers.h"
#include "common.h"

struct user * user_cache = NULL;
volatile sig_atomic_t clear_cache = 0;
volatile sig_atomic_t reopen_logfile = 1;
volatile sig_atomic_t reopen_slowlog = 1;

/* Escape / unescape newlines, tabs and backslash characters
 *  - A static buffer is used to hold the escaped string,
 *	so it needs to be copied if required */

char * escapeString(const char * string, size_t * length) {
	static char * escaped = NULL;
	static size_t escaped_size = 0;
	size_t string_length = strlen(string);
	const char * temp = string;
	char * dest;

	/* Assume we have to escape everything */
	if (escaped_size <= string_length * 2 ) {
		escaped_size = string_length *2;
		escaped = realloc(escaped, escaped_size);
	}

	dest = escaped;

	while (*temp != '\0') {
		switch (*temp) {
			case '\\':
				*dest++ = '\\';
				*dest++ = '\\';
				break;

			case '\n':
				*dest++ = '\\';
				*dest++ = 'n';
				break;

			case '\t':
				*dest++ = '\\';
				*dest++ = 't';
				break;

			default:
				*dest++ = *temp;
				break;
		}

		temp++;
	}

	*dest = '\0';

	if (length)
		*length = dest - escaped;

	return escaped;
}

void unescapeString(char * string) {
	char * temp = string;

	if (string == NULL)
		return;

	while (*temp != '\0') {
		if (*temp != '\\') {
			temp++;
			continue;
		}

		if (*(temp + 1) == 'n')
			*temp = '\n';
		else if (*(temp + 1) == '\\')
			*temp = '\\';
		else if (*(temp + 1) == 't')
			*temp = '\t';

		memmove(temp + 1, temp + 2, strlen(temp + 2) + 1);
		temp++;
	}
}

/* Return the time, in milliseconds
 * Note: This is NOT the real time! It's mainly used for timed events,
 *	   where only the duration between calls is important. */

int64_t getTimeMS(void) {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
	return (tp.tv_sec * 1000) + (tp.tv_nsec / 1000000);
}

/* Return a formated timespec as a static string
 *  - If elapsed is zero, print as year-month-day hour:minute:second.milliseconds
 *	Otherwise print an elapsed time as hours minutes seconds milliseconds
 *	ie 1969-07-21 02:56:15.000
 *	or 2h 31m 40.000s */

char * print_time(const struct timespec * time, int elapsed) {
	static char formatted[64];

	if (elapsed) {
		int len = 0, hours, minutes, seconds;

		hours = time->tv_sec / 3600;
		minutes = (time->tv_sec % 3600) / 60;
		seconds = time->tv_sec % 60;

		if (hours)
			len = snprintf(formatted, sizeof(formatted), "%dh ", hours);

		snprintf(formatted + len, sizeof(formatted) - len, "%dm %d.%03lds", minutes, seconds, time->tv_nsec / 1000000);

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

char *skipChars(char *str, const char *skip) {
	while (*str != '\0' && strchr(skip, *str)) str++;
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

/* Return non-zero if all the characters are printable */
int isprintable(const char * str) {
	int len = strlen(str);

	for (int i = 0; i < len; i++) {
		if (isprint(str[i]) == 0)
			return 0;
	}

	return 1;
}

void * dup_mem(void * src, size_t len, size_t size) {
	size_t alloc_size = len > size ? len + 1 : size;
	void * dst = malloc(alloc_size);

	if (dst == NULL)
		return NULL;

	memcpy(dst, src, len);

	return dst;
}

/* Add the string representation of the supplied int64_t to the buffer provided.
 * Returns the len added to the buffer, excluding the null terminator.
 * Caution - No overflow checking is performed */

int int64tostr(char *dest, int64_t value) {
	uint64_t v;
	int len;
	int neg = 0;

	if (value < 0) {
		v = value * -1;
		neg = 1;
	} else {
		v = value;
	}

	char *p = dest;
	do {
		*p++ = '0' + (v % 10);
		v /= 10;
	} while(v);

	if (neg)
		*p++ = '-';

	*p = '\0';
	len = p - dest;

	p--;

	/* Reverse the string to the correct order */
	char tmp;
	while(dest < p) {
		tmp = *dest;
		*dest = *p;
		*p = tmp;
		dest++;
		p--;
	}

	return len;
}

int matches_wildcard(const char *pattern, const char *string, int wildcard) {
	if (wildcard) {
		if (fnmatch(pattern, string, 0) == 0)
			return 0;
		else
			return 1;
	}

	return strcmp(string, pattern);
}

int matches(const char *pattern, const char *string) {
	return matches_wildcard(pattern, string, (strchr(pattern, '*') || strchr(pattern, '?')));
}

/* Check whether a name (resource, queue) is valid
 * A valid name is essentially just a posix 'fully portable filename' ie: A-Z a-z 0-9 . _ - */
int check_name(char *name) {
	static char *allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-";

	if (strspn(name, allowed) != strlen(name))
		return 1;

	return 0;
}

static int load_users_env(char * username, struct user * u) {
	int pipefd[2];
	int status;
	size_t e_size = 0x2000; //Guess.
	size_t bytes = 0;
	ssize_t len = 0;
	char * e = NULL;

	if (pipe(pipefd) == -1) {
		fprintf(stderr, "Failed to invoke pipe()\n");
		return 1;
	}

	pid_t pid = fork();

	if (pid == -1) {
		perror("fork failed in load_users_env");
		return 1;
	}

	if (pid == 0) {
		char *cmd = NULL;

		asprintf(&cmd, "%s %d", "jers_dump_env", pipefd[1]);

		char * args[] = {"/usr/bin/su", "--login", username, "-c", cmd, NULL};
		int nullfd = open("/dev/null", O_WRONLY);

		if (nullfd < 0) {
			fprintf(stderr, "Failed to open /dev/null: %s\n", strerror(errno));
			_exit(1);
		}

		close(pipefd[0]);

		dup2(nullfd, STDOUT_FILENO);
		dup2(nullfd, STDERR_FILENO);

		execvp(args[0], args);

		fprintf(stderr, "Failed to call jers_dump_env: %s\n", strerror(errno));
		_exit(1);
	}

	/* Parent */
	close(pipefd[1]);

	e = malloc(e_size);

	while ((len = read(pipefd[0], e + bytes, e_size - bytes)) > 0) {
		bytes += len;

		if (bytes == e_size) {
			e_size *= 2;
			e = realloc(e, e_size);
		}
	}

	e[bytes] = '\0';

	waitpid(pid, &status, 0);

	if ((WIFEXITED(status) && WEXITSTATUS(status)) || WIFSIGNALED(status)) {
		fprintf(stderr, "Failed to get user environment: %s\n", username);
		free(e);
		return 1;
	}

	close(pipefd[0]);

	/* Split the buffer into null terminated strings */
	int i = 0;
	char * ptr, * end;

	if (u->env_size == 0)
		u->env_size = 256;

	u->users_env = malloc(sizeof(char *) * u->env_size);

	ptr = e;
	end = e + bytes;

	while (ptr < end) {
		u->users_env[i++] = ptr++;

		if (i == u->env_size) {
			u->env_size *= 2;
			u->users_env = realloc(u->users_env, sizeof(char *) * u->env_size);
		}

		ptr += strlen(ptr) + 1;
	}

	u->users_env[i] = NULL;

	u->env_count = i;
	u->users_env_buffer = e;

	return 0;
}

/* Lookup / populate a users details, caching the results */

struct user * lookup_user(uid_t uid, int load_env) {
	struct user * u = NULL;
	time_t now = time(NULL);
	struct passwd * pw = NULL;
	int update = 0;

	if (clear_cache) {
		clear_cache = 0;
		fprintf(stderr, "Clearing user cache\n");
		freeUserCache();
	}

	if (user_cache) {
		HASH_FIND_INT(user_cache, &uid, u);

		if (u && now < u->expiry) {
			if (!load_env || (load_env && u->users_env != NULL))
				return u;
		}

		if (u)
			update = 1;
	}

	if (u == NULL) {
		u = calloc(1, sizeof(struct user));
	} else {
		free(u->username);
		free(u->shell);
		free(u->home_dir);
		free(u->group_list);
		free(u->users_env);
		free(u->users_env_buffer);
	}

	pw = getpwuid(uid);

	if (!pw) {
		if (update)
			HASH_DEL(user_cache, u);

		free(u);
		return NULL;
	}

	/* Get the users groups */
	u->group_count = 256; // Guess.
	u->group_list = malloc(sizeof(gid_t) * u->group_count);

	if (getgrouplist(pw->pw_name, pw->pw_gid, u->group_list, &u->group_count) < 0) {
		u->group_list = realloc(u->group_list, sizeof(gid_t) * u->group_count);

		if (getgrouplist(pw->pw_name, pw->pw_gid, u->group_list, &u->group_count) < 0) {
			fprintf(stderr, "Failed to get group list for %d (%s): %s\n", uid, pw->pw_name, strerror(errno));

			if (update)
				HASH_DEL(user_cache, u);

			free(u->group_list);
			free(u);
			return NULL;
		}
	}

	if (load_env && load_users_env(pw->pw_name, u) !=  0) {
		fprintf(stderr, "Failed to load users environment: %s\n", pw->pw_name);

		if (update)
			HASH_DEL(user_cache, u);

		free(u->group_list);
		free(u);
		return NULL;
	}

	u->uid = uid;
	u->gid = pw->pw_gid;
	u->expiry = now + CACHE_EXPIRY;
	u->username = strdup(pw->pw_name);
	u->shell = strdup(pw->pw_shell);
	u->home_dir = strdup(pw->pw_dir);
	u->permissions = -1;

	if (!update)
		HASH_ADD_INT(user_cache, uid, u);

	return u;
}

void freeUserCache(void) {
	struct user * u, * user_tmp;

	HASH_ITER(hh, user_cache, u, user_tmp) {
		free(u->username);
		free(u->shell);
		free(u->home_dir);
		free(u->group_list);
		free(u->users_env);
		free(u->users_env_buffer);

		HASH_DEL(user_cache, u);

		free(u);
	}

	user_cache = NULL;
}

void handlerSigsegv(int signum, siginfo_t *info, void *context) {
	void * btrace[100];
	int btrace_size = 0;

	UNUSED(context);

	/* We still use fprintf here, as we are going to crash anyway */
	fprintf(stderr, "====================================================================\n");
	fprintf(stderr, " JERS v%d.%d.%d CRASH - Signal '%s' (Signum:%d)\n", JERS_MAJOR, JERS_MINOR, JERS_PATCH, strsignal(signum), signum);
	fprintf(stderr, "====================================================================\n");

	if (signum == SIGSEGV || signum == SIGBUS || signum == SIGILL || signum == SIGFPE || signum == SIGTRAP)
		fprintf(stderr, "Crash occured accessing memory at address %p\n", info->si_addr);
		//TODO: Show registers (EIP, etc...)

	if (signum == SIGSEGV) {
		fprintf(stderr, "SIGSEGV - %s\n", info->si_code == SEGV_MAPERR ? "address not mapped to object" : info->si_code == SEGV_ACCERR ?
			 "invalid permissions for mapped object" : "????");
	}

	fprintf(stderr, "\nCurrent Stack:\n\n");
	btrace_size = backtrace(btrace, 100);
	backtrace_symbols_fd(btrace, btrace_size, STDERR_FILENO);
	fprintf(stderr, "====================================================================\n");

	/* Reinstate the default handler and send ourselves the same signal so we can produce a core dump */
	struct sigaction sigact;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_NODEFER|SA_RESETHAND;
	sigact.sa_handler = SIG_DFL;
	sigaction(signum, &sigact, NULL);

	raise(signum);
}

void clearCacheHandler(int signum) {
	(void) signum;
	clear_cache = 1;
}

void hupHandler(int signum) {
	(void) signum;
	reopen_logfile = 1;
	reopen_slowlog = 1;
}

void setup_handlers(void(*shutdownHandler)(int)) {
	struct sigaction sigact;

	/* Wrapup & shutdown signals */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = shutdownHandler;

	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);

	/* SIGUSR1 - Drop any cached data */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = clearCacheHandler;

	sigaction(SIGUSR1, &sigact, NULL);

	/* SIGHUP - Close and reopen the current logfile */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = hupHandler;

	sigaction(SIGHUP, &sigact, NULL);

	/* Fatal signals - Display a backtrace if we get these */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
	sigact.sa_sigaction = handlerSigsegv;
	sigaction(SIGSEGV, &sigact, NULL);
	sigaction(SIGBUS, &sigact, NULL);
	sigaction(SIGFPE, &sigact, NULL);
	sigaction(SIGILL, &sigact, NULL);

	return;
}

/* Why isn't this provided somewhere? */
struct _sig_names {
	const char *name;
	const int number;
};

/* Not all these signal will be defined on every system... Fix when it causes a problem */
const struct _sig_names sig_names[] = {
	{"SIGHUP", SIGHUP},
	{"SIGINT", SIGINT},
	{"SIGQUIT", SIGQUIT},
	{"SIGILL", SIGILL},
	{"SIGTRAP", SIGTRAP},
	{"SIGABRT", SIGABRT},
	{"SIGBUS", SIGBUS},
	{"SIGFPE", SIGFPE},
	{"SIGKILL", SIGKILL},
	{"SIGUSR1", SIGUSR1},
	{"SIGSEGV", SIGSEGV},
	{"SIGUSR2", SIGUSR2},
	{"SIGPIPE", SIGPIPE},
	{"SIGALRM", SIGALRM},
	{"SIGTERM", SIGTERM},
	{"SIGSTKFLT", SIGSTKFLT},
	{"SIGCHLD", SIGCHLD},
	{"SIGCONT", SIGCONT},
	{"SIGSTOP", SIGSTOP},
	{"SIGTSTP", SIGTSTP},
	{"SIGTTIN", SIGTTIN},
	{"SIGTTOU", SIGTTOU},
	{"SIGURG", SIGURG},
	{"SIGXCPU", SIGXCPU},
	{"SIGXFSZ", SIGXFSZ},
	{"SIGVTALRM", SIGVTALRM},
	{"SIGPROF", SIGPROF},
	{"SIGWINCH", SIGWINCH},
	{"SIGIO", SIGIO},
	{"SIGPWR", SIGPWR},
	{"SIGSYS", SIGSYS},
	{NULL, 0}
};

const char * getSignalName(int signal) {
	for (int i = 0; sig_names[i].name; i++) {
		if (sig_names[i].number == signal)
			return sig_names[i].name;
	}

	return NULL;
}

int getSignalNumber(const char *name) {
	for (int i = 0; sig_names[i].name; i++) {
		if (strcasecmp(sig_names[i].name, name) == 0)
			return sig_names[i].number;
	}

	return -1;
}

/* Returned a buffer containing the hex encoding of 'input' of length 'input_len'
 * If 'output' is NULL, a buffer will be allocated and returned - This should be freed by the caller
 * If 'output' is non-null, it is expected to be big enough to store the result. ie. (input_len *2) + 1 */
char * hexEncode(const unsigned char *input, int input_len, char *output) {
	const char *_hex = "0123456789ABCDEF";
	char *ret = output ? output : malloc((input_len * 2) + 1);

	if (ret == NULL)
		return NULL;

	char *tmp = ret;

	for (int i = 0; i < input_len; i++) {
		*tmp++ = _hex[(*input>>4) &0xF];
		*tmp++ = _hex[(*input++) &0xF];
	}

	*tmp = 0;

	return ret;
}

/* Split a line from a config file into key/value. */

int splitConfigLine(char *line, char **key, char **value) {
	/* Remove any comments and trailing/leading whitespace */
	char * comment = strchr(line, '#');

	if (comment != NULL)
		*comment = '\0';

	removeWhitespace(line);

	*key = *value = NULL;

	/* Blank Line*/
	if (*line == '\0')
		return 1;

	/* The key and value are seperated by a space */
	*key = line;
	*value = strchr(line, ' ');

	if (*value == NULL)
		return 1;

	**value = '\0';
	(*value)++;

	removeWhitespace(*key);
	removeWhitespace(*value);

	return 0;
}

/* getArg() - Return a pointer to the next argument in 'string'
 * Note: This modifies the contents of 'string' */

char *getArg(char **string) {
	char *arg;
	char *c;
	int state = 0;

	if (string == NULL || *string == NULL)
		return NULL;

	c = skipWhitespace(*string);

	if (*c == '\0')
		return NULL;

	arg = c;

	while (*c) {
		switch (*c) {
			case ' ':
			case '\t':
				if (state == 0)
					goto return_arg;

				break;

			case '\'':
			case '"':
				if (state == *c) {
					goto return_arg;
				}
				else if (state == 0) {
					if (arg == c)
						arg++;

					state = *c;
				}
		}

		c++;
	}

	*string = c;
	return arg;

return_arg:
	*c = '\0';
	*string = c + 1;
	return arg;
}

/* Parse a string, seperating the tokens based on 'sep'
 * Returns a NULL terminated malloced array of pointers
 * The input string is modified */
char **seperateTokens(char *string, char sep) {
	size_t count = 0;
	char **tokens = NULL;
	char *p, *start;

	for (p = string; *p; p++) {
		if (*p == sep)
			count++;
	}

	count++;
	tokens = malloc(sizeof(char*) * (count + 1));

	if (tokens == NULL)
		return NULL;

	count = 0;
	start = string;

	for (p = string; *p; p++) {
		if (*p == sep) {
			*p = '\0';
			tokens[count++] = removeWhitespace(start);
			p++;
			start = p;
		}
	}

	if (*start)
		tokens[count++] = removeWhitespace(start);

	tokens[count] = NULL;

	return tokens;
}

#define DEFAULT_ITEM_SIZE 32

void listNew(struct item_list *list, size_t item_size) {
	memset(list, 0, sizeof(struct item_list));
	list->item_size = item_size;
}

int listAdd(struct item_list *list, void *item) {
	if (list->count == list->size) {
		size_t new_size = list->size == 0 ? DEFAULT_ITEM_SIZE : (list->size * 2);

		void *new_list = realloc(list->items, new_size * list->item_size);

		if (new_list == NULL)
			return 1;

		list->items = new_list;
		list->size = new_size;
		list->end  = (char *)list->items + (list->count * list->item_size);
	}

	memcpy(list->end, item, list->item_size);
	list->end = (char *)list->end + list->item_size;
	list->count++;

	return 0;
}

void listFree(struct item_list *list) {
	free(list->items);
	memset(list, 0, sizeof(struct item_list));
	return;
}

void listSort(struct item_list *list, int (*compar)(const void *, const void *, void *), void *arg) {
	qsort_r(list->items, list->count, list->item_size, compar, arg);
}

/* Return a key/value from the provided line
 * 'line' is modified and the pointers returned should not be freed */

int loadKeyValue (char *line, char **key, char ** value, int *index) {
	char *l_key = NULL;
	char *l_value = NULL;

	/* Skip leading whitespace */
	char *ptr = skipWhitespace(line);

	if (*ptr == '\0' || *ptr == '#') {
		*key = NULL;
		*value = NULL;
		return 0;
	}

	l_key = ptr;

	ptr = skipUntilWhitespace(ptr);
	*ptr = '\0';

	l_value = ptr + 1;

	*key = l_key;
	*value = l_value;

	/* Check if the key has an index */
	ptr = l_key;
	while (*ptr != '\0' && *ptr != '[') ptr++;

	if (*ptr == '[') {
		*ptr = '\0';

		if (index)
			*index = atoi(ptr + 1);
	}

	unescapeString(l_value);

	return 0;
}
