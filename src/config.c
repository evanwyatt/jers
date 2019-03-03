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

#include <server.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <grp.h>

#define GROUP_LIMIT 32

static gid_t getGroup(char * name) {
	struct group * g = getgrnam(name);

	if (g == NULL)
		error_die("Invalid read_group specified: %s", strerror(errno));

	return g->gr_gid;
}
/* Convert a space seperated list of groups into an array of gids
 * Note: There is a hard limit of 32 groups per security. */

void getGroups(char * groups, struct gid_array * array) {
	int count = 0;
	array->groups = malloc(sizeof(gid_t) * GROUP_LIMIT);

	if (array->groups == NULL)
		error_die("Failed to allocate space for security groups: %s", strerror(errno));

	char * tok = strtok(groups, " ");

	while (tok) {
		if (*tok == '\0') {
			tok = strtok(NULL, " ");
			continue;
		}

		if (count + 1 > GROUP_LIMIT)
			error_die("Too many groups specified for security group");

		array->groups[count++] = getGroup(tok);
		tok = strtok(NULL, " ");
	}

	array->count = count;
}

void loadConfig(char * config) {
	FILE * f = NULL;
	char * line = NULL;
	size_t line_size = 0;
	ssize_t len = 0;

	if (config == NULL)
		config = DEFAULT_CONFIG_FILE;
	
	f = fopen(config, "r");

	if (f == NULL) {
		error_die("Failed to open config file: %s\n", strerror(errno));
	}

	/* Populate the defaults before we start parsing the config file */
	server.state_fd = -1;
	server.state_dir = strdup(DEFAULT_CONFIG_STATEDIR);
	server.background_save_ms = DEFAULT_CONFIG_BACKGROUNDSAVEMS;
	server.logging_mode = DEFAULT_CONFIG_LOGGINGMODE;
	server.event_freq = DEFAULT_CONFIG_EVENTFREQ;
	server.sched_freq = DEFAULT_CONFIG_SCHEDFREQ;
	server.sched_max = DEFAULT_CONFIG_SCHEDMAX;
	server.max_run_jobs = DEFAULT_CONFIG_MAXJOBS;
	server.max_cleanup = DEFAULT_CONFIG_MAXCLEAN;
	server.highest_jobid = DEFAULT_CONFIG_HIGHJOBID;
	server.socket_path = strdup(DEFAULT_CONFIG_SOCKETPATH);
	server.agent_socket_path = strdup(DEFAULT_CONFIG_AGENTSOCKETPATH);

	server.flush.defer = DEFAULT_CONFIG_FLUSHDEFER;
	server.flush.defer_ms = DEFAULT_CONFIG_FLUSHDEFERMS;

	server.logging_mode = JERS_LOG_DEBUG;

	while ((len = getline(&line, &line_size, f)) != -1) {
		line[strcspn(line, "\n")] = '\0';

		/* Remove any comments and trailing/leading whitespace */
		char * comment = strchr(line, '#');
		if (comment != NULL)
			*comment = '\0';
		
		removeWhitespace(line);
		/* Blank Line*/
		if (*line == '\0')
			continue;

		/* The key and value are seperated by a space */
		char * key = line;
		char * value = strchr(line, ' ');

		if (value == NULL) {
			print_msg(JERS_LOG_WARNING, "Skipping unknown line: %ld %s\n", strlen(line),line);
			continue;	  
		}

		*value = '\0';
		value++;

		removeWhitespace(key);
		removeWhitespace(value);

		if (strcmp(key, "state_dir") == 0) {
			free(server.state_dir);
			server.state_dir = strdup(value);
		} else if (strcmp(key, "flush_defer") == 0) {
			if (strcasecmp(value, "yes") == 0)
				server.flush.defer = 1;
			else
				server.flush.defer = 0;

		} else if (strcmp(key, "flush_defer_ms") == 0) {
			server.flush.defer_ms = atoi(value);
		} else if (strcmp(key, "background_save_ms") == 0) {
			server.background_save_ms = atoi(value);
		} else if (strcmp(key, "event_freq") == 0) {
			server.event_freq = atoi(value);
		} else if (strcmp(key, "sched_freq") == 0) {
			server.sched_freq = atoi(value);
		} else if (strcmp(key, "sched_max") == 0) {
			server.sched_max = atoi(value);
		} else if (strcmp(key, "max_system_jobs") == 0) {
			server.max_run_jobs = atoi(value);
		} else if (strcmp(key, "max_jobid") == 0) {
			server.highest_jobid = atoi(value);
		} else if (strcmp(key, "max_clean_job") == 0) {
			server.max_cleanup = atoi(value);
		} else if (strcmp(key, "client_listen_socket") == 0) {
			free(server.socket_path);
			server.socket_path = strdup(value);
		} else if (strcmp(key, "agent_listen_socket") == 0) {
			free(server.agent_socket_path);
			server.agent_socket_path = strdup(value);
		} else if (strcmp(key, "logfile") == 0) {
			server.logfile = strdup(value);
		} else if (strcmp(key, "read_group") == 0) {
			getGroups(value, &server.permissions.read);
		} else if (strcmp(key, "write_group") == 0) {
			getGroups(value, &server.permissions.write);
		} else if (strcmp(key, "setuid_group") == 0) {
			getGroups(value, &server.permissions.setuid);
		} else if (strcmp(key, "queue_group") == 0) {
			getGroups(value, &server.permissions.queue);
		} else {
			print_msg(JERS_LOG_WARNING, "Skipping unknown config key: %s\n", key);
			continue;
		}
	}

	free(line);

	if (feof(f) == 0) {
		error_die("Failed to load config file: %s\n", strerror(errno));
	}

	return;
}
