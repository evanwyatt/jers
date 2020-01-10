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

#ifndef __JERS_COMMON_H
#define __JERS_COMMON_H

#include <sys/types.h>

#include <uthash.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#define UNUSED(x) (void)(x)

#define CONST_STRLEN(x) (sizeof(x) - sizeof(x[0]))

#ifdef __linux
#define USE_SETPROCTITLE
#define INIT_SETPROCTITLE_REPLACEMENT
void spt_init(int argc, char *argv[]);
void setproctitle(const char *fmt, ...);
#endif

#define PERM_READ   0x01
#define PERM_WRITE  0x02
#define PERM_SETUID 0x04
#define PERM_QUEUE  0x08
#define PERM_SELF   0x10

#define PERM_NOTUSED 0x8000

#define CMDFLG_REPLAY 0x01

/* Exit code flags to indicate issues between agent and daemon */
#define JERS_EXIT_FAIL (1<<24)   // Job failed to start
#define JERS_EXIT_SIGNAL (1<<25) // Job was signaled
#define JERS_EXIT_STATUS 0       // Exit code from job
#define JERS_EXIT_STATUS_MASK 0xFF

struct user {
	uid_t uid;
	gid_t gid;

	int permissions;

	char * username;
	char * shell;
	char * home_dir;

	int group_count;
	gid_t * group_list;

	int env_count;				// Number of variables being used in users_env
	int env_size;  				// Number of variables that can be stored in user_env
	char ** users_env;			// Variables - NULL terminated
	char * users_env_buffer;	// Buffer storing the cached variables for the user

	time_t expiry;

	UT_hash_handle hh;
};

#define CACHE_EXPIRY 900 // 15 minutes

int64_t getTimeMS(void);

char * escapeString(const char * string, size_t * length);
void unescapeString(char * string);
char * removeWhitespace(char * str);
char *skipChars(char *str, const char *skip);
char *skipWhitespace(char *str);

void uppercasestring(char * str);
void lowercasestring(char * str);
int isprintable(const char * str);
void * dup_mem(void * src, size_t len, size_t size);

int int64tostr(char * dest, int64_t num);
char * gethost(void);

int matches(const char * pattern, const char * string);

char * print_time(const struct timespec * time, int elapsed);
void timespec_diff(const struct timespec *start, const struct timespec *end, struct timespec *diff);

int check_name(char *name);

struct user * lookup_user(uid_t uid, int load_env);
void freeUserCache(void);

const char * getSignalName(int signal);
int getSignalNumber(const char *name);

void setup_handlers(void(*shutdownHandler)(int));
char * hexEncode(const unsigned char *input, int input_len, char *output);
int splitConfigLine(char *line, char **key, char **value);
#endif
