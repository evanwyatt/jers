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
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <server.h>
#include <comms.h>
#include <commands.h>
#include <fields.h>

void runSimpleCommand(client * c);
void runComplexCommand(client * c);

command_t commands[] = {
	{"ADD_JOB",      command_add_job,      deserialize_add_job},
	{"GET_JOB",      command_get_job,      deserialize_get_job},
	{"MOD_JOB",      command_mod_job,      deserialize_mod_job},
	{"DEL_JOB",      command_del_job,      deserialize_del_job},
	{"ADD_QUEUE",    command_add_queue,    deserialize_add_queue},
	{"GET_QUEUE",    command_get_queue,    deserialize_get_queue},
	{"MOD_QUEUE",    command_mod_queue,    deserialize_mod_queue},
	{"DEL_QUEUE",    command_del_queue,    deserialize_del_queue},
	{"ADD_RESOURCE", command_add_resource, deserialize_add_resource},
	{"GET_RESOURCE", command_get_resource, deserialize_get_resource},
	{"MOD_RESOURCE", command_mod_resource, deserialize_mod_resource},
	{"DEL_RESOURCE", command_del_resource, deserialize_del_resource},
	{"STATS",        command_stats,        NULL},
};

/* Append to or allocate a new reponse buffer */

void appendResponse(client * c, char * buffer, size_t length) {
	buffAdd(&c->response, buffer, length);
	setWritable(&c->connection);
}

//TODO: Update this function to accept a error type + string
void appendError(client * c, char * msg) {
	appendResponse(c, msg, strlen(msg));
}

/* Run either a simple or complex command, which will
 * populate a response to send back to the client */

int runCommand(client * c) {
	if (c->msg.version == 0) {
		runSimpleCommand(c);
	} else {
		runComplexCommand(c);
	}

	return 0;
}

void runSimpleCommand(client * c) {
	resp_t * response = NULL;

	 if (strcmp(c->msg.command, "PING") == 0) {
		response = respNew();
		respAddSimpleString(response, "PONG");
	} else {
		fprintf(stderr, "Unknown command passed: %s\n", c->msg.command);
		return;
	}

	if (response) {
		size_t length = 0;
		char * str =  respFinish(response, &length);
		appendResponse(c, str, length);
		free(str);
	}

	free_message(&c->msg, NULL);
}

void runComplexCommand(client * c) {
	static int cmd_count = sizeof(commands) / sizeof(command_t);
	int i;
	uint64_t start, end;

	start = getTimeMS();

	if (c->msg.command == NULL) {
		fprintf(stderr, "Bad request from client\n");
		return;
	}

	/* Match the command name */
	for (i = 0; i < cmd_count; i++) {
		if (strcmp(c->msg.command, commands[i].name) == 0) {

			void * args = NULL;

			if (commands[i].deserialize_func) {
				args = commands[i].deserialize_func(&c->msg);

				if (!args) {
					fprintf(stderr, "Failed to deserialize %s args\n", c->msg.command);
					return;
				}
			}

			commands[i].cmd_func(c, args);

			if (commands[i].free_func)
				commands[i].free_func(args);

			break;
		}
	}

	end = getTimeMS();

	print_msg(JERS_LOG_DEBUG, "Command '%s' took %ldms\n", c->msg.command, end - start);

	free_message(&c->msg, NULL);
}

int command_stats(client * c, void * args) {
	resp_t * r = respNew();
	respAddArray(r);
	respAddSimpleString(r, "RESP");
	respAddInt(r, 1);
	respAddMap(r);

	addIntField(r, STATSRUNNING, server.stats.running);
	addIntField(r, STATSPENDING, server.stats.pending);
	addIntField(r, STATSDEFERRED, server.stats.deferred);
	addIntField(r, STATSHOLDING, server.stats.holding);
	addIntField(r, STATSCOMPLETED, server.stats.completed);
	addIntField(r, STATSEXITED, server.stats.exited);

	respCloseMap(r);
	respCloseArray(r);

	size_t reply_length = 0;
	char * reply = respFinish(r, &reply_length);
	appendResponse(c, reply, reply_length);
	free(reply);

	return 0;
}

void command_agent_login(agent * a) {
	/* We should only have a NODE field */
	if (a->msg.items[0].field_count != 1 || a->msg.items[0].fields[0].number != NODE) {
		print_msg(JERS_LOG_WARNING, "Got invalid login from agent");
		return;
	}

	a->host = getStringField(&a->msg.items[0].fields[0]);

	print_msg(JERS_LOG_INFO, "Got login from agent on host %s", a->host);

	/* Check the queues and link this agent against queues matching this host */

	struct queue * q;
	for (q = server.queueTable; q != NULL; q = q->hh.next) {
		if (strcmp(q->host, "localhost") == 0) {
			if (strcmp(gethost(), a->host) == 0)
				q->agent = a;
		} else if (strcmp(q->host, a->host) == 0) {
			q->agent = a;
		}

		if (q->agent)
			print_msg(JERS_LOG_DEBUG, "Enabling queue %s\n", q->name);

		//HACK:
		q->state |= JERS_QUEUE_FLAG_STARTED;
	}
}

void command_agent_jobstart(agent * a) {
	int64_t i;
	jobid_t jobid = 0;
	pid_t pid = -1;
	struct job * j = NULL;

	for (i = 0; i < a->msg.items[0].field_count; i++) {
		switch(a->msg.items[0].fields[i].number) {
			case JOBID : jobid = getNumberField(&a->msg.items[0].fields[i]); break;
			case JOBPID: pid = getNumberField(&a->msg.items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",a->msg.items[0].fields[i].name); break;
		}
	}

	HASH_FIND_INT(server.jobTable, &jobid, j);

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got job start for non-existent jobid: %d", jobid);
		return;
	}

	j->state = JERS_JOB_RUNNING;
	j->internal_state &= ~JERS_JOB_FLAG_STARTED;
	j->pid = pid;

	print_msg(JERS_LOG_DEBUG, "JobID: %d started PID:%d", jobid, pid);

	return;
}

void command_agent_jobcompleted(agent * a) {
	jobid_t jobid = 0;
	int exitcode = 0;
	int i;
	struct job * j = NULL;

	for (i = 0; i < a->msg.items[0].field_count; i++) {
		switch(a->msg.items[0].fields[i].number) {
			case JOBID : jobid = getNumberField(&a->msg.items[0].fields[i]); break;
			case EXITCODE: exitcode = getNumberField(&a->msg.items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",a->msg.items[0].fields[i].name); break;
		}
	}

	HASH_FIND_INT(server.jobTable, &jobid, j);

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got job completed for non-existent jobid: %d", jobid);
		return;
	}

	if (WIFEXITED(exitcode)) {
		j->exitcode = WEXITSTATUS(exitcode);
	} else if (WIFSIGNALED(exitcode)) {
		j->signal = WTERMSIG(exitcode);
		j->exitcode = 128 + j->signal;
	}

	j->pid = -1;

	changeJobState(j, j->exitcode ? JERS_JOB_EXITED : JERS_JOB_COMPLETED, 1);

	print_msg(JERS_LOG_DEBUG, "JobID: %d %s exitcode:%d", jobid, exitcode ? "EXITED" : "COMPLETED", j->exitcode);

	return;
}

int runAgentCommand(agent * a) {
	printf("Got AGENT command: %s\n", a->msg.command);

	if (strcasecmp(a->msg.command, "AGENT_LOGIN") == 0) {
		command_agent_login(a);
	} else if (strcasecmp(a->msg.command, "JOB_STARTED") == 0) {
		command_agent_jobstart(a);
	} else if (strcasecmp(a->msg.command, "JOB_COMPLETED") == 0){
		command_agent_jobcompleted(a);
	} else {
		print_msg(JERS_LOG_WARNING, "Invalid agent command %s received", a->msg.command);
	}

	free_message(&a->msg, &a->requests);

	return 0;
}
