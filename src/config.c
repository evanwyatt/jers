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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>

#include "agent.h"

extern agent *agentList;

static gid_t getGroup(char * name) {
	struct group * g = getgrnam(name);

	if (g == NULL)
		error_die("Invalid group specified: %s", strerror(errno));

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

void loadAllowedAgents(char * agents_string) {
	/* The agent string should be a space seperated list of hostnames
	 * that we expect agents to connect from */
	char * tok = strtok(agents_string, " ");

	while (tok) {
		print_msg(JERS_LOG_DEBUG, "Adding agent '%s' to allowed list", tok);
		agent *a = calloc(1, sizeof(agent));

		if (a == NULL)
			error_die("Failed to allocate space for agents: %s", strerror(errno));

		a->host = strdup(tok);
		a->connection.socket = -1;
		addAgent(a);

		tok = strtok(NULL, " ");
	}

	return;
}

int cmp_queue_acl(const void *a, const void *b, void *arg) {
	(void)arg;

	return ((struct queue_acl *)b)->allow - ((struct queue_acl *)a)->allow;
}

void loadQueueACL(char *in_acl) {
	struct queue_acl acl = {0};
	int group_count = 0;
	print_msg_info("Loading queue ACL: %s", in_acl);

	/* A ACL should be in the format of: '<allow/disallow> <permission[,permission...]> <queue_expr[,queue_expr...]> <group[,group...]'
	 * These are loaded into the server memory and evaluated when a queue is loaded. */
	char *allow, *permissions, *expressions, *groups;
	char *tok = strtok(in_acl, " ");

	if (tok == NULL)
		error_die("Invalid queue_perm acl specified");

	allow = tok;

	tok = strtok(NULL, " ");

	if (tok == NULL)
		error_die("Invalid queue_perm acl specified");

	permissions = tok;

	tok = strtok(NULL, " ");

	if (tok == NULL)
		error_die("Invalid queue_perm acl specified");

	expressions = tok;

	tok = strtok(NULL, " ");

	if (tok == NULL)
		error_die("Invalid queue_perm acl specified");

	groups = tok;

	/* Allow/Disallow */
	if (strcasecmp(allow, "ALLOW") == 0)
		acl.allow = 1;
	else if (strcasecmp(allow, "DENY") != 0)
		error_die("Invalid allow field '%s' in queue_perm", allow);

	/* Permissions */
	char *p = strtok(permissions, ",");
	while (p) {
		if (strcasecmp(p, "control") == 0) {
			acl.permissions |= QUEUE_CONTROL;
		} else if (strcasecmp(p, "limit") == 0) {
			acl.permissions |= QUEUE_LIMIT;
		} else if (strcasecmp(p, "admin") == 0) {
			acl.permissions |= QUEUE_ADMIN;
			break;
		} else {
			error_die("Invalid permission in queue_acl: %s", p);
		}

		p = strtok(NULL, ",");
	}

	/* Groups */
	p = strtok(groups, ",");
	while (p) {
		if (group_count + 1 > GROUP_LIMIT)
			error_die("Too many groups specified for queue acl (max %d)", GROUP_LIMIT);

		acl.gids[group_count++] = getGroup(p);
		p = strtok(NULL, ",");
	}

	/* Add an entry for each expression */
	p = strtok(expressions, ",");
	while (p) {
		acl.expr = strdup(p);

		listAdd(&server.queue_acls, &acl);

		p = strtok(NULL, ",");
	}
}

void freeConfig(void) {
	free(server.state_dir);
	free(server.socket_path);
	free(server.agent_socket_path);

	free(server.permissions.read.groups);
	free(server.permissions.write.groups);
	free(server.permissions.setuid.groups);
	free(server.permissions.queue.groups);
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
	server.journal.fd = -1;
	server.state_dir = strdup(DEFAULT_CONFIG_STATEDIR);
	server.background_save_ms = DEFAULT_CONFIG_BACKGROUNDSAVEMS;
	server.logging_mode = DEFAULT_CONFIG_LOGGINGMODE;
	server.event_freq = DEFAULT_CONFIG_EVENTFREQ;
	server.sched_freq = DEFAULT_CONFIG_SCHEDFREQ;
	server.sched_max = DEFAULT_CONFIG_SCHEDMAX;
	server.max_run_jobs = DEFAULT_CONFIG_MAXJOBS;
	server.max_cleanup = DEFAULT_CONFIG_MAXCLEAN;
	server.max_jobid = DEFAULT_CONFIG_MAXJOBID;
	server.socket_path = strdup(DEFAULT_CONFIG_SOCKETPATH);
	server.agent_socket_path = strdup(DEFAULT_CONFIG_AGENTSOCKETPATH);
	server.acct_socket_path = strdup(DEFAULT_CONFIG_ACCTSOCKETPATH);

	server.journal.extend_block_size = JOURNAL_EXTEND_DEFAULT;

	server.flush.defer = DEFAULT_CONFIG_FLUSHDEFER;
	server.flush.defer_ms = DEFAULT_CONFIG_FLUSHDEFERMS;

	server.email_freq_ms = DEFAULT_CONFIG_EMAIL_FREQ;

	server.default_job_nice = JERS_JOB_DEFAULT_NICE;

	server.slowrequest_logging = SLOWREQUEST_ON;
	server.slow_threshold_ms = DEFAULT_SLOWLOG;

	listNew(&server.queue_acls, sizeof(struct queue_acl));

	while ((len = getline(&line, &line_size, f)) != -1) {
		line[strcspn(line, "\n")] = '\0';
		char *key, *value;

		if (splitConfigLine(line, &key, &value))
			continue;

		if (key == NULL || value == NULL) {
			print_msg(JERS_LOG_WARNING, "Skipping unknown line: %ld %s\n", strlen(line),line);
			continue;
		}

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
			server.max_jobid = atoi(value);
		} else if (strcmp(key, "max_clean_job") == 0) {
			server.max_cleanup = atoi(value);
		} else if (strcmp(key, "client_listen_socket") == 0) {
			free(server.socket_path);
			server.socket_path = strdup(value);
		} else if (strcmp(key, "agent_listen_port") == 0) {
			server.agent_port = atoi(value);

			if (server.agent_port == 0)
				print_msg(JERS_LOG_WARNING, "Invalid agent_listen_port specified in config file %s'. Not listening via TCP port", value);

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
		} else if (strcmp(key, "self_group") == 0) {
			getGroups(value, &server.permissions.self);
		} else if (strcmp(key, "queue_admin") == 0 || strcmp(key, "queue_group") == 0) {
			getGroups(value, &server.permissions.queue);
		} else if (strcmp(key, "agents") == 0) {
			loadAllowedAgents(value);
		} else if (strcmp(key, "logging_mode") == 0) {
			if (strcmp(value, "DEBUG") == 0)
				server.logging_mode = JERS_LOG_DEBUG;
			else if (strcasecmp(value, "INFO") == 0)
				server.logging_mode = JERS_LOG_INFO;
			else if (strcasecmp(value, "WARNING") == 0)
				server.logging_mode = JERS_LOG_WARNING;
			else if (strcasecmp(value, "CRITICAL") == 0)
				server.logging_mode = JERS_LOG_CRITICAL;
			else {
				print_msg(JERS_LOG_WARNING, "Unknown logging mode '%s' specified in config file. Defaulting to 'INFO'", value);
				server.logging_mode = JERS_LOG_INFO;
			}
		} else if(strcmp(key, "secret") == 0) {
			server.secret = 1;

			if (loadSecret(value, server.secret_hash) != 0)
				error_die("Unable to load secret specified in configuration file: %s", value);
		} else if (strcmp(key, "auto_cleanup") == 0) {
			server.auto_cleanup = atoi(value);
		} else if (strcmp(key, "default_job_nice") == 0) {
			server.default_job_nice = atoi(value);

			if (server.default_job_nice < -20 || server.default_job_nice > 19)
				server.default_job_nice = JERS_JOB_DEFAULT_NICE;

			if (server.default_job_nice < 0)
				print_msg(JERS_LOG_WARNING, "Warning - Setting a default nice value below 0 is not recommended");

		} else if (strcmp(key, "slowrequest_threshold") == 0) {
			if (strcasecmp(value, "off") == 0) {
				server.slowrequest_logging = SLOWREQUEST_OFF;
				continue;
			} else if (strcasecmp(value, "all") == 0) {
				server.slowrequest_logging = SLOWREQUEST_ALL;
				continue;
			}

			server.slow_threshold_ms = atoi(value);
		} else if (strcmp(key, "index_tag") == 0) {
			server.index_tag = strdup(value);
		} else if (strcmp(key, "queue_acl") == 0) {
			loadQueueACL(value);
		} else {
			print_msg(JERS_LOG_WARNING, "Skipping unknown config key: %s\n", key);
			continue;
		}
	}

	free(line);

	if (feof(f) == 0) {
		error_die("Failed to load config file: %s\n", strerror(errno));
	}

	fclose(f);

	if (agentList == NULL) {
		/* No allowed agents specified in the config file,
		 * only allow an agent connection from localhost */
		agentList = calloc(1, sizeof(agent));
		agentList->host = strdup(gethost());
		agentList->connection.socket = -1;

		print_msg(JERS_LOG_WARNING, "No agents in config file. Only allowing an agent from localhost");
	}

	/* Sort the loaded queue ACLs */
	if (server.queue_acls.count != 0)
		listSort(&server.queue_acls, cmp_queue_acl, NULL);

	return;
}
