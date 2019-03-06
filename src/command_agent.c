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

void command_agent_login(agent * a, msg_t * msg) {
	/* We should only have a NODE field */
	if (msg->items[0].field_count != 1 || msg->items[0].fields[0].number != NODE) {
		print_msg(JERS_LOG_WARNING, "Got invalid login from agent");
		return;
	}

	a->host = getStringField(&msg->items[0].fields[0]);

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

void command_agent_jobstart(agent * a, msg_t * msg) {
	int64_t i;
	jobid_t jobid = 0;
	pid_t pid = -1;
	time_t start_time = 0;
	struct job * j = NULL;

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
		return;
	}

	changeJobState(j, JERS_JOB_RUNNING, 0);

	j->internal_state &= ~JERS_JOB_FLAG_STARTED;
	j->pend_reason = 0;
	j->pid = pid;
	j->start_time = start_time;

    if (server.recovery.in_progress && j->res_count) {
		for (i = 0; i < j->res_count; i++)
			j->req_resources[i].res->in_use += j->req_resources[i].needed;
	}

	server.stats.total.started++;
	print_msg(JERS_LOG_DEBUG, "JobID: %d started PID:%d", jobid, pid);

	return;
}

void command_agent_jobcompleted(agent * a, msg_t * msg) {
	jobid_t jobid = 0;
	int exitcode = 0;
	time_t finish_time = 0;
	int i;
	struct job * j = NULL;

	for (i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].number) {
			case JOBID : jobid = getNumberField(&msg->items[0].fields[i]); break;
			case FINISHTIME: finish_time = getNumberField(&msg->items[0].fields[i]); break;
			case EXITCODE: exitcode = getNumberField(&msg->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", msg->items[0].fields[i].name); break;
		}
	}

	j = findJob(jobid);

	if (j == NULL) {
		print_msg(JERS_LOG_WARNING, "Got job completed for non-existent jobid: %d", jobid);
		return;
	}

	if (j->internal_state & JERS_JOB_FLAG_STARTED) {
		/* Job failed to start correctly */
		j->internal_state &= ~JERS_JOB_FLAG_STARTED;
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

	changeJobState(j, j->exitcode ? JERS_JOB_EXITED : JERS_JOB_COMPLETED, 1);

	if (exitcode)
		server.stats.total.exited++;
	else
		server.stats.total.completed++;

	print_msg(JERS_LOG_INFO, "JobID: %d %s exitcode:%d finish_time:%d", jobid, exitcode ? "EXITED" : "COMPLETED", j->exitcode, j->finish_time);

	return;
}