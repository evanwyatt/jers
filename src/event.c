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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "client.h"
#include "agent.h"
#include "acct.h"
#include "email.h"

#define MINUTE_MS(x) (60000 * x)

struct event {
	void (*func)(void);
	int interval;    // Milliseconds between event triggering
	int64_t last_fire;
	struct event * next;
};

struct event * eventList = NULL;

void registerEvent(void(*func)(void), int interval) {
	struct event * e = calloc(sizeof(struct event), 1);

	e->func = func;
	e->interval = interval;

	/* Add it to the linked list of events */
	e->next = eventList;
	eventList = e;
	return;
}

void freeEvents(void) {
	struct event *e = eventList;

	while (e) {
		struct event *next = e->next;
		free(e);
		e = next;
	}
}

void checkClientEvent(void) {
	client * c = clientList;

	/* Check the connected clients for commands to action,
	 * we can limit the amount of time we spend running command here. */

	while (c) {
		if (c->request.used == 0) {
			c = c->next;
			continue;
		}

		/* Check if the client has a full request to process */
		char *nl = memchr(c->request.data, '\n', c->request.used);

		if (nl == NULL) {
			c = c->next;
			continue;
		}

		*nl = '\0';
		nl++;

		if (load_message(c->request.data, &c->msg)) {
			client * c_next = c->next;
			print_msg(JERS_LOG_WARNING, "Failed to load client request, disconnecting them.");
			handleClientDisconnect(c);
			c = c_next;
			continue;
		}

		runCommand(c);

		/* Remove the used data from the clients request stream */
		buffRemove(&c->request, (size_t)(nl - c->request.data), 0);

		c = c->next;
	}
}

void checkAgentEvent(void) {
	agent * a = agentList;

	while (a) {
		agent * a_next = a->next;
		size_t consumed = 0;
		char *p = a->requests.data;

		while (1) {
			char *nl = memchr(p, '\n', a->requests.used - consumed);

			if (nl == NULL)
				break;

			*nl = '\0';
			nl++;

			size_t request_len = nl - p;

			if (load_message(p, &a->msg)) {
				print_msg(JERS_LOG_WARNING, "Failed to load agent message - Disconnecting them");
				handleAgentDisconnect(a);
				break;		
			}

			if (runAgentCommand(a) != 0)
				break;

			consumed += request_len;
			p += request_len;
		}

		buffRemove(&a->requests, consumed, 0);
		a = a_next;
	}
}

void checkAcctEvent(void) {
	acctClient *a = acctClientList;

	while (a) {
		acctClient *a_next = a->next;
		int status;

		/* Check if the child has exited, and clean up the resources, etc. */
		if (waitpid(a->pid, &status, WNOHANG) == a->pid) {
			if (WIFEXITED(status)) {

			} else if (WIFSIGNALED(status)) {

			} else {

			}

			/* Close the connection and remove the client */
			print_msg(JERS_LOG_INFO, "Closing connection to accounting client pid:%d fd:%d uid:%d", a->pid, a->connection.socket, a->uid);
			close(a->connection.socket);
			removeAcctClient(a);
		}

		a = a_next;
	}

}

void checkDeferEvent(void) {
	releaseDeferred();
}

void flushEvent(void) {
	flush_journal(0);
}

void checkJobsEvent(void) {
	checkJobs();
}

void cleanupEvent(void) {
	uint32_t cleaned = 0;
	uint32_t max_clean = server.max_cleanup;

	/* If we are busy, don't try and clean up as many deleted items */
	if (server.candidate_pool_jobs || server.candidate_recalc)
		max_clean = (max_clean + 1) / 2;

	cleaned += cleanupJobs(max_clean);

	if (cleaned >= max_clean)
		return;

	cleaned += cleanupQueues(max_clean - cleaned);

	if (cleaned >= max_clean)
		return;

	cleanupResources(max_clean - cleaned);
}

void backgroundSaveEvent(void) {
	stateSaveToDisk(0);
}

/* Send any pending emails */
void checkEmails(void) {
	checkEmailProcesses();
}

void autoCleanup(void) {
	time_t target_time = time(NULL) - (server.auto_cleanup * 60 * 60);

	for (struct job *j = server.jobTable; j != NULL; j = j->hh.next) {
		if (j->internal_state &JERS_FLAG_DELETED || j->state != JERS_JOB_COMPLETED)
			continue;

		if (j->finish_time <= target_time)
			deleteJob(j);
	}
}

void initEvents(void) {
	registerEvent(checkJobsEvent, server.sched_freq);
	registerEvent(cleanupEvent, 1000);
	registerEvent(backgroundSaveEvent, server.background_save_ms);

	if (server.flush.defer)
		registerEvent(flushEvent, server.flush.defer_ms);

	registerEvent(checkDeferEvent, 750);

	registerEvent(checkEmails, server.email_freq_ms);

	registerEvent(checkAgentEvent, 0);
	registerEvent(checkClientEvent, 0);
	registerEvent(checkAcctEvent, 1000);

	if (server.auto_cleanup != 0)
		registerEvent(autoCleanup, MINUTE_MS(5));
}

/* Check for any timed events to expire */

void checkEvents(void) {
	struct event * e = eventList;

	int64_t now = getTimeMS();

	while (e) {
		if (now >= e->last_fire + e->interval) {
			e->func();
			e->last_fire = getTimeMS();
		}

		e = e->next;
	}
}
