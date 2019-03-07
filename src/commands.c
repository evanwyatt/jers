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

const char * getErrType(int jers_error);

void runSimpleCommand(client * c);
void runComplexCommand(client * c);

command_t commands[] = {
	{CMD_ADD_JOB,      PERM_WRITE,            CMDFLG_REPLAY, command_add_job,      deserialize_add_job, free_add_job},
	{CMD_GET_JOB,      PERM_READ,             0,          command_get_job,      deserialize_get_job, free_get_job},
	{CMD_MOD_JOB,      PERM_WRITE,            CMDFLG_REPLAY, command_mod_job,      deserialize_mod_job, free_mod_job},
	{CMD_DEL_JOB,      PERM_WRITE,            CMDFLG_REPLAY, command_del_job,      deserialize_del_job, free_del_job},
	{CMD_SIG_JOB,      PERM_WRITE,            0,          command_sig_job,      deserialize_sig_job, free_sig_job},
	{CMD_ADD_QUEUE,    PERM_WRITE|PERM_QUEUE, CMDFLG_REPLAY, command_add_queue,    deserialize_add_queue, free_add_queue},
	{CMD_GET_QUEUE,    PERM_READ,             0,          command_get_queue,    deserialize_get_queue, free_get_queue},
	{CMD_MOD_QUEUE,    PERM_WRITE|PERM_QUEUE, CMDFLG_REPLAY, command_mod_queue,    deserialize_mod_queue, free_mod_queue},
	{CMD_DEL_QUEUE,    PERM_WRITE|PERM_QUEUE, CMDFLG_REPLAY, command_del_queue,    deserialize_del_queue, free_del_queue},
	{CMD_ADD_QUEUE, PERM_WRITE,            CMDFLG_REPLAY, command_add_resource, deserialize_add_resource, free_add_resource},
	{CMD_GET_QUEUE, PERM_READ,             0,          command_get_resource, deserialize_get_resource, free_get_resource},
	{CMD_MOD_QUEUE, PERM_WRITE,            CMDFLG_REPLAY, command_mod_resource, deserialize_mod_resource, free_mod_resource},
	{CMD_DEL_QUEUE, PERM_WRITE,            CMDFLG_REPLAY, command_del_resource, deserialize_del_resource, free_del_resource},
	{CMD_SET_TAG,      PERM_WRITE,            CMDFLG_REPLAY, command_set_tag,      deserialize_set_tag, free_set_tag},
	{CMD_DEL_TAG,      PERM_WRITE,            CMDFLG_REPLAY, command_del_tag,      deserialize_del_tag, free_del_tag},
	{CMD_STATS,        PERM_READ,             0,          command_stats,        NULL, NULL},
};

agent_command_t agent_commands[] = {
	{AGENT_JOB_STARTED,   CMDFLG_REPLAY, command_agent_jobstart},
	{AGENT_JOB_COMPLETED, CMDFLG_REPLAY, command_agent_jobcompleted},
	{AGENT_LOGIN,   0,          command_agent_login},
	{AGENT_RECON_RESP,    0,          command_agent_recon},
};

command_t * sorted_commands = NULL;
agent_command_t * sorted_agentcommands = NULL;

int cmpCommand(const void * _a, const void * _b) {
	const command_t * a = _a;
	const command_t * b = _b;

	return strcmp(a->name, b->name);
}

int cmpAgentCommand(const void * _a, const void * _b) {
	const agent_command_t * a = _a;
	const agent_command_t * b = _b;

	return strcmp(a->name, b->name);
}

void sortAgentCommands(void) {
	int cmd_count = sizeof(agent_commands) / sizeof(agent_command_t);
	sorted_agentcommands = malloc(cmd_count * sizeof(agent_command_t));

	memcpy(sorted_agentcommands, agent_commands, cmd_count * sizeof(agent_command_t));

	qsort(sorted_agentcommands, cmd_count, sizeof(agent_command_t), cmpAgentCommand);
}

void sortCommands(void) {
	int cmd_count = sizeof(commands) / sizeof(command_t);
	sorted_commands = malloc(cmd_count * sizeof(command_t));

	memcpy(sorted_commands, commands, cmd_count * sizeof(command_t));

	qsort(sorted_commands, cmd_count, sizeof(command_t), cmpCommand);
}

void runComplexCommand(client * c) {
	static int cmd_count = sizeof(commands) / sizeof(command_t);
	uint64_t start, end;
	command_t * command_to_run = NULL;
	void * args = NULL;

	start = getTimeMS();

	if (unlikely(c->msg.command == NULL)) {
		fprintf(stderr, "Bad request from client\n");
		return;
	}

	if (likely(sorted_commands != NULL)) {
		command_t lookup;
		lookup.name = c->msg.command;

		command_to_run = bsearch(&lookup, sorted_commands, cmd_count, sizeof(command_t), cmpCommand);
	} else {
		for (int i = 0; i < cmd_count; i++) {
			if (strcmp(c->msg.command, commands[i].name) == 0) {
				command_to_run = &commands[i];
				break;
			}
		}
	}

	if (unlikely(command_to_run == NULL)) {
		print_msg(JERS_LOG_WARNING, "Got unknown command from client");
		return;
	}

	if (validateUserAction(c, command_to_run->perm) != 0) {
		sendError(c, JERS_ERR_NOPERM, NULL);
		print_msg(JERS_LOG_DEBUG, "User %d not authorized to run %s", c->uid, c->msg.command);
		return;
	}

	if (likely(command_to_run->deserialize_func != NULL)) {
		args = command_to_run->deserialize_func(&c->msg);

		if (unlikely(args == NULL)) {
			print_msg(JERS_LOG_WARNING, "Failed to deserialize %s args\n", c->msg.command);
			return;
		}
	}

	int status = command_to_run->cmd_func(c, args);

	/* Write to the journal if the transaction was an update and successful */
	if (command_to_run->flags &CMDFLG_REPLAY && status == 0)
		stateSaveCmd(c->uid, c->msg.command, c->msg.reader.msg_cpy, c->msg.jobid, c->msg.revision);

	if (likely(command_to_run->free_func != NULL))
		command_to_run->free_func(args);

	end = getTimeMS();

	print_msg(JERS_LOG_DEBUG, "Command '%s' took %ldms\n", c->msg.command, end - start);

	free_message(&c->msg);
}

int runAgentCommand(agent * a) {
	static int cmd_count = sizeof(agent_commands) / sizeof(agent_command_t);
	agent_command_t * command_to_run = NULL;

	if (likely(sorted_agentcommands != NULL)) {
		agent_command_t lookup;
		lookup.name = a->msg.command;

		command_to_run = bsearch(&lookup, sorted_agentcommands, cmd_count, sizeof(agent_command_t), cmpAgentCommand);
	} else {
		for (int i = 0; i < cmd_count; i++) {
			if (strcmp(a->msg.command, agent_commands[i].name) == 0) {
				command_to_run = &agent_commands[i];
				break;
			}
		}
	}

	if (likely(command_to_run != NULL)) {
		command_to_run->cmd_func(a, &a->msg);
	} else {
		print_msg(JERS_LOG_WARNING, "Invalid agent command %s received", a->msg.command);
		return 0;
	}

	/* Write to the journal if the transaction was an update and successful */
	if (command_to_run->flags &CMDFLG_REPLAY)
		stateSaveCmd(0, a->msg.command, a->msg.reader.msg_cpy, 0, 0);

	free_message(&a->msg);
	
	if (buffRemove(&a->requests, a->msg.reader.pos, 0)) {
			a->msg.reader.pos = 0;
			respReadUpdate(&a->msg.reader, a->requests.data, a->requests.used);
	}

	return 0;
}

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
	const char * error_type = getErrType(error);

	if (c == NULL) {
		if (server.recovery.in_progress)
			return;
		
		print_msg(JERS_LOG_WARNING, "Trying to send a error code, but no client provided");
		return;
	}

	respNew(&response);

	snprintf(str, sizeof(str), "%s %s", error_type, err_msg ? err_msg : "");

	respAddSimpleError(&response, str);

	_sendMessage(&c->connection, &c->response, &response);
}

int sendClientReturnCode(client * c, const char * ret) {
	resp_t r;

	if (c == NULL) {
		if (server.recovery.in_progress)
			return 0;
		
		print_msg(JERS_LOG_WARNING, "Trying to send a return code, but no client provided");
		return 1;
	}

	respNew(&r);
	respAddSimpleString(&r, ret);

	return _sendMessage(&c->connection, &c->response, &r);
}

int sendClientMessage(client * c, resp_t * r) {
	respCloseArray(r);

	if (c == NULL) {
		if (server.recovery.in_progress)
			return 0;
		
		print_msg(JERS_LOG_WARNING, "Trying to send a response, but no client provided");
		return 1;
	}

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
	free_message(&c->msg);
}

void replayCommand(msg_t * msg) {
	static int cmd_count = sizeof(commands) / sizeof(command_t);
	static int agent_cmd_count = sizeof(commands) / sizeof(agent_command_t);
	int done = 0;

	if (msg->command == NULL)
		return;

	/* Match the command name */
	for (int i = 0; i < cmd_count; i++) {
		if (strcmp(msg->command, commands[i].name) == 0) {
			void * args = NULL;

			if ((commands[i].flags &CMDFLG_REPLAY) == 0)
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

			done = 1;
			break;
		}
	}

	if (done == 0) {
		/* Agent commands to replay */
		for (int i = 0; i < agent_cmd_count; i++) {
			if (strcmp(msg->command, agent_commands[i].name) == 0) {
				agent_commands[i].cmd_func(NULL, msg);
				break;
			}
		}
	}

	free_message(msg);
	
	return;
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
	addIntField(&r, STATSEXITED, server.stats.jobs.unknown);

	addIntField(&r, STATSTOTALSUBMITTED, server.stats.total.submitted);
	addIntField(&r, STATSTOTALSTARTED, server.stats.total.started);
	addIntField(&r, STATSTOTALCOMPLETED, server.stats.total.completed);
	addIntField(&r, STATSTOTALEXITED, server.stats.total.exited);
	addIntField(&r, STATSTOTALDELETED, server.stats.total.deleted);
	addIntField(&r, STATSTOTALUNKNOWN, server.stats.total.unknown);


	respCloseMap(&r);

	return sendClientMessage(c, &r);
}
