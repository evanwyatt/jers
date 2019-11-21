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
#include <commands.h>
#include <json.h>

int command_agent_login(agent * a, msg_t * msg) {
	UNUSED(msg);
	print_msg(JERS_LOG_INFO, "Got login from agent on host %s", a->host);

	/* Check the queues and link this agent against queues matching this host */
	for (struct queue *q = server.queueTable; q != NULL; q = q->hh.next) {
		if (strcmp(q->host, "localhost") == 0) {
			if (strcmp(gethost(), a->host) == 0) {
				q->agent = a;
				print_msg(JERS_LOG_DEBUG, "Assigned localhost queue '%s' to host %s", q->name, a->host);
			}
		} else if (strcmp(q->host, a->host) == 0) {
			q->agent = a;
		}
	}

	/* If we had a secret specified in the configuration file, send an auth challenge */
	if (server.secret) {
		buff_t auth_challenge;
		char * nonce = generateNonce(NONCE_SIZE);

		if (nonce == NULL)
			return 1;

		initRequest(&auth_challenge, AGENT_AUTH_CHALLENGE, 1);
		JSONAddString(&auth_challenge, NONCE, nonce);

		sendAgentMessage(a, &auth_challenge);

		a->nonce = nonce;
	} else {
		/* Request a reconciliation from the agent */
		buff_t recon;
		initRequest(&recon, "RECON_REQ", 1);
		sendAgentMessage(a, &recon);
		print_msg(JERS_LOG_INFO, "Requested recon from %s\n", a->host);

		a->recon = 1;
		a->logged_in = 1;
	}

	return 0;
}

int command_agent_authresp(agent * a, msg_t * msg) {
	char *c_nonce = NULL;
	char *hmac = NULL;
	time_t datetime = 0;
	time_t time_now = time(NULL);
	char datetime_str[16];
	char *verify_hmac = NULL;

	print_msg(JERS_LOG_INFO, "Got authresp message from agent on host: %s", a->host);

	//Expecting a nonce and a hmac
	for (int i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].number) {
			case NONCE : c_nonce = getStringField(&msg->items[0].fields[i]); break;
			case DATETIME: datetime = getNumberField(&msg->items[0].fields[i]); break;
			case MSG_HMAC  : hmac = getStringField(&msg->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[0].fields[i].name); break;
		}
	}

	if (c_nonce == NULL) {
		print_msg(JERS_LOG_WARNING, "Agent authresp did not contain a client_nonce. - Disconnecting them");
		goto auth_fail;
	}

	if (hmac == NULL) {
		print_msg(JERS_LOG_WARNING, "Agent authresp did not contain a HMAC. - Disconnecting them");
		goto auth_fail;
	}

	/* The datetime needs to be within our tolerences */
	if (datetime == 0) {
		print_msg(JERS_LOG_WARNING, "Agent authresp did not contain a Datetime. - Disconnecting them");
		goto auth_fail;
	}
	if (datetime < time_now || datetime > time_now + MAX_AUTH_TIME) {
		print_msg(JERS_LOG_WARNING, "Agent authresp datetime out of allowed tolerances. datetime:%ld now:%ld tolerance:%ds - Disconnecting them",
			datetime, time_now, MAX_AUTH_TIME);
		goto auth_fail;
	}

	sprintf(datetime_str, "%ld", datetime);

	/* Verify the client knew the shared secret by verifying the HMAC on the response */
	const char *input[] = {a->nonce, c_nonce, datetime_str, NULL};
	verify_hmac = generateHMAC(input, server.secret_hash, sizeof(server.secret_hash));

	if (verify_hmac == NULL) {
		print_msg(JERS_LOG_WARNING, "Agent authresp Failed to generated a HMAC. - Disconnecting agent.");
		goto auth_fail;
	}

	if (strcasecmp(verify_hmac, hmac) != 0) {
		print_msg(JERS_LOG_WARNING, "Agent authresp HMAC incorrect. - Disconnecting them");
		print_msg(JERS_LOG_DEBUG, "Our HMAC: %s Theirs:%s Datetime:%s c_nonce:%s", verify_hmac, hmac, datetime_str, c_nonce);
		goto auth_fail;
	}

	/* Client looks legitimate, send a recon request.
	 * We include a HMAC of the client nonce and the datetime to
	 * allow the client to validate we know the secret */
	buff_t recon;
	initRequest(&recon, "RECON_REQ", 1);

	sprintf(datetime_str, "%ld", time_now);
	const char *reconInput[] = {c_nonce, datetime_str, NULL};
	char *recon_hmac = generateHMAC(reconInput, server.secret_hash, sizeof(server.secret_hash));

	JSONAddInt(&recon, DATETIME, time_now);
	JSONAddString(&recon, MSG_HMAC, recon_hmac);

	sendAgentMessage(a, &recon);

	print_msg(JERS_LOG_INFO, "Requested recon from %s\n", a->host);
	print_msg(JERS_LOG_DEBUG, "datetime:%s hmac:%s", datetime_str, recon_hmac);

	a->recon = 1;
	a->logged_in = 1;

	free(verify_hmac);
	free(c_nonce);
	free(hmac);
	free(recon_hmac);

	return 0;

auth_fail:
	free(verify_hmac);
	free(c_nonce);
	free(hmac);
	return 1;
}

int command_agent_recon(agent * a, msg_t * msg) {
	print_msg(JERS_LOG_INFO, "Got recon message from agent on host: %s", a->host);

	for (int i = 0; i < msg->item_count; i++) {
		jobid_t jobid = 0;
		time_t start_time = 0;
		time_t finish_time = 0;
		pid_t pid = 0;
		int exitcode = 0;
		struct rusage usage = {{0}};

		for (int k = 0; k < msg->items[i].field_count; k++) {
			switch(msg->items[i].fields[k].number) {
				case JOBID : jobid = getNumberField(&msg->items[i].fields[k]); break;
				case STARTTIME: start_time = getNumberField(&msg->items[i].fields[k]); break;
				case FINISHTIME: finish_time = getNumberField(&msg->items[i].fields[k]); break;
				case JOBPID: pid = getNumberField(&msg->items[i].fields[k]); break;
				case EXITCODE: exitcode = getNumberField(&msg->items[i].fields[k]); break;

				case USAGE_UTIME_SEC : usage.ru_utime.tv_sec = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_UTIME_USEC: usage.ru_utime.tv_usec = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_STIME_SEC : usage.ru_stime.tv_sec = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_STIME_USEC: usage.ru_stime.tv_usec = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_MAXRSS    : usage.ru_maxrss = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_MINFLT    : usage.ru_minflt = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_MAJFLT    : usage.ru_majflt = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_INBLOCK   : usage.ru_inblock = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_OUBLOCK   : usage.ru_oublock = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_NVCSW     : usage.ru_nvcsw = getNumberField(&msg->items[i].fields[k]); break;
				case USAGE_NIVCSW    : usage.ru_nivcsw = getNumberField(&msg->items[i].fields[k]); break;

				default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[i].fields[k].name); break;
			}
		}

		print_msg(JERS_LOG_INFO, "Recon - JobID:%d StartTime:%ld FinishTime:%ld JobPID:%d ExitCode:%d", jobid, start_time, finish_time, pid, exitcode);

		/* Update the details of the job */
		struct job * j = findJob(jobid);

		/* There is a slim chance that the main daemon doesn't have the job in memory, while an agent does.
		 * This can be caused if the transaction journal didn't get flushed to disk before the job
		 * was sent to the agent, then the node the main daemon was on has crashed and 'lost' that job. Solutions would involve
		 * ensuring the journal had been flushed prior to the job being started - TODO */

		if (j == NULL)
			error_die("Agent '%s' has sent recon for job %d, but we don't have it in memory.", a->host, jobid);

		/* Remove the pending reason, we know the job is no longer pending */
		j->pend_reason = 0;

		if (start_time) {
			j->start_time = start_time;
		}

		if (finish_time) {
			j->finish_time = finish_time;
			j->exitcode = exitcode;
			changeJobState(j, exitcode ? JERS_JOB_EXITED : JERS_JOB_COMPLETED, NULL, 1);
		}

		if (pid) {
			j->pid = pid;

			/* Need to update the resources consumed by this job */
			if (j->res_count) {
				for (int res_idx = 0; res_idx < j->res_count; res_idx++)
					j->req_resources[res_idx].res->in_use += j->req_resources[res_idx].needed;
			}

			changeJobState(j, JERS_JOB_RUNNING, NULL, 0);
		}

		/* Update the usage info */
		memcpy(&j->usage, &usage, sizeof(struct rusage));
	}

	/* Make sure this is committed to disk */
	flush_journal(1);

	/* Reply to the agent letting it know we've processed this message */
	buff_t fin;
	initRequest(&fin, "RECON_COMPLETE", 1);
	sendAgentMessage(a, &fin);

	a->recon = 0;

	return 0;
}

int command_agent_jobstart(agent * a, msg_t * msg) {
	int64_t i;
	jobid_t jobid = 0;
	pid_t pid = -1;
	time_t start_time = 0;
	struct job * j = NULL;

	UNUSED(a);

	for (i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].number) {
			case JOBID : jobid = getNumberField(&msg->items[0].fields[i]); break;
			case STARTTIME: start_time = getNumberField(&msg->items[0].fields[i]); break;
			case JOBPID: pid = getNumberField(&msg->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[0].fields[i].name); break;
		}
	}

	j = findJob(jobid);

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got job start for non-existent jobid: %d", jobid);
		return 1;
	}

	changeJobState(j, JERS_JOB_RUNNING, NULL, 1);

	j->internal_state &= ~JERS_FLAG_JOB_STARTED;
	j->pend_reason = 0;
	j->pid = pid;
	j->start_time = start_time;

	if (server.recovery.in_progress && j->res_count) {
		for (i = 0; i < j->res_count; i++)
			j->req_resources[i].res->in_use += j->req_resources[i].needed;
	}

	server.stats.total.started++;
	print_msg(JERS_LOG_DEBUG, "JobID: %d started PID:%d", jobid, pid);

	return 0;
}

int command_agent_jobcompleted(agent * a, msg_t * msg) {
	jobid_t jobid = 0;
	int exitcode = 0;
	time_t finish_time = 0;
	int i;
	struct job * j = NULL;
	struct rusage usage = {{0}};

	UNUSED(a);

	for (i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].number) {
			case JOBID : jobid = getNumberField(&msg->items[0].fields[i]); break;
			case FINISHTIME: finish_time = getNumberField(&msg->items[0].fields[i]); break;
			case EXITCODE: exitcode = getNumberField(&msg->items[0].fields[i]); break;

			case USAGE_UTIME_SEC : usage.ru_utime.tv_sec = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_UTIME_USEC: usage.ru_utime.tv_usec = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_STIME_SEC : usage.ru_stime.tv_sec = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_STIME_USEC: usage.ru_stime.tv_usec = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_MAXRSS    : usage.ru_maxrss = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_MINFLT    : usage.ru_minflt = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_MAJFLT    : usage.ru_majflt = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_INBLOCK   : usage.ru_inblock = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_OUBLOCK   : usage.ru_oublock = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_NVCSW     : usage.ru_nvcsw = getNumberField(&msg->items[0].fields[i]); break;
			case USAGE_NIVCSW    : usage.ru_nivcsw = getNumberField(&msg->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[0].fields[i].name); break;
		}
	}

	j = findJob(jobid);

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got job completed for non-existent jobid: %d", jobid);
		return 1;
	}

	if (j->internal_state & JERS_FLAG_JOB_STARTED) {
		/* Job failed to start correctly */
		j->internal_state &= ~JERS_FLAG_JOB_STARTED;
		print_msg(JERS_LOG_WARNING, "Got completion for job without start: %d", jobid);
	}

	if (j->res_count) {
		for (i = 0; i < j->res_count; i++)
			j->req_resources[i].res->in_use -= j->req_resources[i].needed;
	}

	j->exitcode = exitcode &JERS_EXIT_STATUS_MASK;

	if (exitcode & JERS_EXIT_FAIL) {
		j->fail_reason = j->exitcode;
		j->exitcode = 255;
	} else if (exitcode & JERS_EXIT_SIGNAL) {
		j->signal = exitcode &JERS_EXIT_STATUS_MASK;
		j->exitcode += 128;
	}

	j->pid = -1;
	j->finish_time = finish_time;

	memcpy(&j->usage, &usage, sizeof(struct rusage));

	changeJobState(j, j->exitcode ? JERS_JOB_EXITED : JERS_JOB_COMPLETED, NULL, 1);

	if (exitcode)
		server.stats.total.exited++;
	else
		server.stats.total.completed++;

	print_msg(JERS_LOG_INFO, "JobID: %d %s exitcode:%d finish_time:%ld", jobid, exitcode ? "EXITED" : "COMPLETED", j->exitcode, j->finish_time);

	return 0;
}

int command_agent_proxyconn(agent *a, msg_t *msg) {
	pid_t pid = -1;
	uid_t uid = -1;

	for (int i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].number) {
			case PID : pid = getNumberField(&msg->items[0].fields[i]); break;
			case UID: uid = getNumberField(&msg->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[0].fields[i].name); break;
		}
	}

	client *c = NULL;
	
	/* Check if we already had this client connected, if so remove the old client */
	for (c = clientList; c; c = c->next) {
		if (c->connection.proxy.pid == 0)
			continue;

		if (c->connection.proxy.agent == a && c->connection.proxy.pid == pid)
			break;
	}
	
	if (c != NULL) {
		print_msg(JERS_LOG_WARNING, "Had previous proxy client connected on agent %d pid:%d", ((agent *)c->connection.proxy.agent)->host, c->connection.proxy.pid);
		buffFree(&c->response);
		buffFree(&c->request);
		removeClient(c);
		free(c);
	}
	
	c = calloc(sizeof(client), 1);

	if (c == NULL)
		error_die("Failed to allocate memory for proxy client request");

	c->uid = uid;
	c->connection.proxy.pid = pid;
	c->connection.proxy.agent =  a;

	buffNew(&c->request, 0);

	addClient(c);

	return 0;
}

int command_agent_proxydata(agent *a, msg_t *msg) {
	pid_t pid = -1;
	char *data = NULL;

	for (int i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].number) {
			case PID : pid = getNumberField(&msg->items[0].fields[i]); break;
			case PROXYDATA: data = getStringField(&msg->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[0].fields[i].name); break;
		}
	}

	if (data == NULL) {
		return 1;
	}

	fprintf(stderr, "Proxydata = %s\n", data);

	/* Locate the client structure */
	client *c = NULL;
	for (c = clientList; c; c = c->next) {
		if (c->connection.proxy.pid == 0)
			continue;

		if (c->connection.proxy.agent == a && c->connection.proxy.pid == pid)
			break;
	}

	if (c == NULL) {
		print_msg(JERS_LOG_WARNING, "Failed to locate proxy client from agent %s pid:%d", a, pid);
		return 1;
	}

	/* Add the new data to the clients stream */
	buffAdd(&c->request, data, strlen(data));
	free(data);

	return 0;
}

int command_agent_proxyclose(agent *a, msg_t *msg) {
	pid_t pid = -1;

	for (int i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].number) {
			case PID : pid = getNumberField(&msg->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[0].fields[i].name); break;
		}
	}

	/* Locate the client structure */
	client *c = NULL;
	for (c = clientList; c; c = c->next) {
		if (c->connection.proxy.pid == 0)
			continue;

		if (c->connection.proxy.agent == a && c->connection.proxy.pid == pid)
			break;
	}

	if (c == NULL) {
		print_msg(JERS_LOG_WARNING, "Failed to locate proxy client (close) from agent %s pid:%d", a, pid);
		return 1;
	}

	buffFree(&c->response);
	buffFree(&c->request);
	removeClient(c);
	free(c);

	return 0;
}