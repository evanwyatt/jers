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
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <server.h>
#include <comms.h>
#include <commands.h>
#include <fields.h>
#include <error.h>

void runSimpleCommand(client * c);
void runComplexCommand(client * c);

command_t commands[] = {
	{"ADD_JOB",      PERM_WRITE,            CMD_REPLAY, command_add_job,      deserialize_add_job, free_add_job},
	{"GET_JOB",      PERM_READ,             0,          command_get_job,      deserialize_get_job, free_get_job},
	{"MOD_JOB",      PERM_WRITE,            CMD_REPLAY, command_mod_job,      deserialize_mod_job, free_mod_job},
	{"DEL_JOB",      PERM_WRITE,            CMD_REPLAY, command_del_job,      deserialize_del_job, free_del_job},
	{"SIG_JOB",      PERM_WRITE,            0,          command_sig_job,      deserialize_sig_job, free_sig_job},
	{"ADD_QUEUE",    PERM_WRITE|PERM_QUEUE, CMD_REPLAY, command_add_queue,    deserialize_add_queue, free_add_queue},
	{"GET_QUEUE",    PERM_READ,             0,          command_get_queue,    deserialize_get_queue, free_get_queue},
	{"MOD_QUEUE",    PERM_WRITE|PERM_QUEUE, CMD_REPLAY, command_mod_queue,    deserialize_mod_queue, free_mod_queue},
	{"DEL_QUEUE",    PERM_WRITE|PERM_QUEUE, CMD_REPLAY, command_del_queue,    deserialize_del_queue, free_del_queue},
	{"ADD_RESOURCE", PERM_WRITE,            CMD_REPLAY, command_add_resource, deserialize_add_resource, free_add_resource},
	{"GET_RESOURCE", PERM_READ,             0,          command_get_resource, deserialize_get_resource, free_get_resource},
	{"MOD_RESOURCE", PERM_WRITE,            CMD_REPLAY, command_mod_resource, deserialize_mod_resource, free_mod_resource},
	{"DEL_RESOURCE", PERM_WRITE,            CMD_REPLAY, command_del_resource, deserialize_del_resource, free_del_resource},
	{"SET_TAG",      PERM_WRITE,            CMD_REPLAY, command_set_tag,      deserialize_set_tag, free_set_tag},
	{"DEL_TAG",      PERM_WRITE,            CMD_REPLAY, command_del_tag,      deserialize_del_tag, free_del_tag},
	{"STATS",        PERM_READ,             0,          command_stats,        NULL, NULL},
};

static int _sendMessage(struct connectionType * connection, buff_t * b, resp_t * r) {
	size_t buff_length = 0;
	char * buff = respFinish(r, &buff_length);

	if (buff == NULL)
		return 1;

	buffAdd(b, buff, buff_length);
	setWritable(connection);
	free(buff);

	return 0;
}

void sendError(client * c, int error, const char * err_msg) {
	resp_t response;
	char str[1024];

	respNew(&response);

	snprintf(str, sizeof(str), "%s %s", error, err_msg ? err_msg : "");

	respAddSimpleError(&response, str);

	_sendMessage(&c->connection, &c->response, &response);
}

int sendClientReturnCode(client * c, const char * ret) {
	resp_t r;
	respNew(&r);
	respAddSimpleString(&r, ret);
	
	return _sendMessage(&c->connection, &c->response, &r);
}

int sendClientMessage(client * c, resp_t * r) {
	respCloseArray(r);

	return _sendMessage(&c->connection, &c->response, r);
}

void sendAgentMessage(agent * a, resp_t * r) {
	respCloseArray(r);

	_sendMessage(&a->connection, &a->responses, r);
	return;
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
	resp_t response;

	 if (strcmp(c->msg.command, "PING") == 0) {
		respNew(&response);
		respAddSimpleString(&response, "PONG");
	} else {
		fprintf(stderr, "Unknown command passed: %s\n", c->msg.command);
		return;
	}

	_sendMessage(&c->connection, &c->response, &response);
	free_message(&c->msg, NULL);
}

void replayCommand(msg_t * msg) {
	static int cmd_count = sizeof(commands) / sizeof(command_t);

	/* Match the command name */
	for (int i = 0; i < cmd_count; i++) {
		if (strcmp(msg->command, commands[i].name) == 0) {
			void * args = NULL;

			if ((commands[i].flags &CMD_REPLAY) == 0)
				break;

			if (commands[i].deserialize_func) {
				args = commands[i].deserialize_func(msg);

				if (!args)
					error_die("Failed to deserialize %s args\n", msg->command);
			}

			if (commands[i].cmd_func(NULL, args) != 0)
				error_die("Failed to replay command");

			if (commands[i].free_func)
				commands[i].free_func(args);

			break;
		}
	}
}

void runComplexCommand(client * c) {
	static int cmd_count = sizeof(commands) / sizeof(command_t);
	uint64_t start, end;

	start = getTimeMS();

	if (c->msg.command == NULL) {
		fprintf(stderr, "Bad request from client\n");
		return;
	}

	/* Match the command name */
	for (int i = 0; i < cmd_count; i++) {
		if (strcmp(c->msg.command, commands[i].name) == 0) {
			void * args = NULL;

			if (validateUserAction(c, commands[i].perm) != 0) {
				sendError(c, JERS_ERR_NOPERM, NULL);
				print_msg(JERS_LOG_DEBUG, "User %d not authorized to run %s", c->uid, c->msg.command);
				break;
			}

			if (commands[i].deserialize_func) {
				args = commands[i].deserialize_func(&c->msg);

				if (!args) {
					fprintf(stderr, "Failed to deserialize %s args\n", c->msg.command);
					break;
				}
			}

			int status = commands[i].cmd_func(c, args);

			/* Write to the journal if the transaction was an update and successful */
			if (commands[i].perm &PERM_WRITE && status == 0)
				stateSaveCmd(c->uid, c->msg.command, c->msg.items[0].field_count, c->msg.items[0].fields, c->msg.out_field_count, c->msg.out_fields);

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
	UNUSED(args);
	resp_t r;

	initMessage(&r, "RESP", 1);

	respAddMap(&r);

	addIntField(&r, STATSRUNNING, server.stats.jobs.running);
	addIntField(&r, STATSPENDING, server.stats.jobs.pending);
	addIntField(&r, STATSDEFERRED, server.stats.jobs.deferred);
	addIntField(&r, STATSHOLDING, server.stats.jobs.holding);
	addIntField(&r, STATSCOMPLETED, server.stats.jobs.completed);
	addIntField(&r, STATSEXITED, server.stats.jobs.exited);

	addIntField(&r, STATSTOTALSUBMITTED, server.stats.total.submitted);
	addIntField(&r, STATSTOTALSTARTED, server.stats.total.started);
	addIntField(&r, STATSTOTALCOMPLETED, server.stats.total.completed);
	addIntField(&r, STATSTOTALEXITED, server.stats.total.exited);
	addIntField(&r, STATSTOTALDELETED, server.stats.total.deleted);

	respCloseMap(&r);

	return sendClientMessage(c, &r);
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
	}
}

void command_agent_jobstart(agent * a) {
	int64_t i;
	jobid_t jobid = 0;
	pid_t pid = -1;
	time_t start_time = 0;
	struct job * j = NULL;

	for (i = 0; i < a->msg.items[0].field_count; i++) {
		switch(a->msg.items[0].fields[i].number) {
			case JOBID : jobid = getNumberField(&a->msg.items[0].fields[i]); break;
			case STARTTIME: start_time = getNumberField(&a->msg.items[0].fields[i]); break;
			case JOBPID: pid = getNumberField(&a->msg.items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",a->msg.items[0].fields[i].name); break;
		}
	}

	HASH_FIND_INT(server.jobTable, &jobid, j);

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got job start for non-existent jobid: %d", jobid);
		return;
	}

	changeJobState(j, JERS_JOB_RUNNING, 0);

	j->internal_state &= ~JERS_JOB_FLAG_STARTED;
	j->pid = pid;
	j->start_time = start_time;

	stateSaveCmd(0, a->msg.command, a->msg.items[0].field_count, a->msg.items[0].fields, 0, NULL);

	server.stats.total.started++;
	print_msg(JERS_LOG_DEBUG, "JobID: %d started PID:%d", jobid, pid);

	return;
}

void command_agent_jobcompleted(agent * a) {
	jobid_t jobid = 0;
	int exitcode = 0;
	time_t finish_time = 0;
	int i;
	struct job * j = NULL;

	for (i = 0; i < a->msg.items[0].field_count; i++) {
		switch(a->msg.items[0].fields[i].number) {
			case JOBID : jobid = getNumberField(&a->msg.items[0].fields[i]); break;
			case FINISHTIME: finish_time = getNumberField(&a->msg.items[0].fields[i]); break;
			case EXITCODE: exitcode = getNumberField(&a->msg.items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",a->msg.items[0].fields[i].name); break;
		}
	}

	HASH_FIND_INT(server.jobTable, &jobid, j);

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got job completed for non-existent jobid: %d", jobid);
		return;
	}

	if (j->res_count) {
		for (i = 0; i < j->res_count; i++)
			j->req_resources[i].res->in_use -= j->req_resources[i].needed;
	}

	if (WIFEXITED(exitcode)) {
		j->exitcode = WEXITSTATUS(exitcode);
	} else if (WIFSIGNALED(exitcode)) {
		j->signal = WTERMSIG(exitcode);
		j->exitcode = 128 + j->signal;
	}

	j->pid = -1;
	j->finish_time = finish_time;

	changeJobState(j, j->exitcode ? JERS_JOB_EXITED : JERS_JOB_COMPLETED, 1);

	stateSaveCmd(0, a->msg.command, a->msg.items[0].field_count, a->msg.items[0].fields, 0, NULL);

	if (exitcode)
		server.stats.total.exited++;
	else
		server.stats.total.completed++;

	print_msg(JERS_LOG_INFO, "JobID: %d %s exitcode:%d finish_time:%d", jobid, exitcode ? "EXITED" : "COMPLETED", j->exitcode, j->finish_time);

	return;
}

int runAgentCommand(agent * a) {
	if (strcasecmp(a->msg.command, "AGENT_LOGIN") == 0) {
		command_agent_login(a);
	} else if (strcasecmp(a->msg.command, "JOB_STARTED") == 0) {
		command_agent_jobstart(a);
	} else if (strcasecmp(a->msg.command, "JOB_COMPLETED") == 0){
		command_agent_jobcompleted(a);
	} else {
		print_msg(JERS_LOG_WARNING, "Invalid agent command %s received", a->msg.command);
	}

	free_message(&a->msg, NULL);

	if (buffRemove(&a->requests, a->msg.reader.pos, 0)) {
			a->msg.reader.pos = 0;
			respReadUpdate(&a->msg.reader, a->requests.data, a->requests.used);
	}

	return 0;
}
