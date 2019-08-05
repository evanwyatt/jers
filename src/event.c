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

void checkClientEvent(void) {
	client * c = server.client_list;
	int rc;

	/* Check the connected clients for commands to action,
	 * we can limit the amount of time we spend running command here.
	 * If we action a command and the client has more data, reinitalise
	 * the reader structure to run the next command */

	while (c) {
		rc = load_message(&c->msg, &c->request);

		if (rc < 0) {
			client * c_next = c->next;
			print_msg(JERS_LOG_WARNING, "Failed to load client request, disconnecting them.");
			handleClientDisconnect(c);
			c = c_next;
			continue;
		}

		if (rc == 0)
			runCommand(c);

		c = c->next;
	}
}

void checkAgentEvent(void) {
	agent * a = server.agent_list;

	while (a) {
		int rc = 0;
		agent * a_next = a->next;

		while (rc == 0) {
			rc = load_message(&a->msg, &a->requests);

			if (rc < 0) {
				print_msg(JERS_LOG_WARNING, "Failed to load agent message - Disconnecting them");
				handleAgentDisconnect(a);
				break;
			}

			if (rc == 0) {
				if (runAgentCommand(a) != 0)
					break;
			}
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

	cleaned += cleanupJobs(server.max_cleanup);

	if (cleaned >= server.max_cleanup)
		return;

	cleaned += cleanupQueues(server.max_cleanup - cleaned);

	if (cleaned >= server.max_cleanup)
		return;

	cleanupResources(server.max_cleanup - cleaned);
}

void backgroundSaveEvent(void) {
	stateSaveToDisk(0);
}

void initEvents(void) {
	registerEvent(checkJobsEvent, server.sched_freq);
	registerEvent(cleanupEvent, 1000);
	registerEvent(backgroundSaveEvent, server.background_save_ms);

	if (server.flush.defer)
		registerEvent(flushEvent, server.flush.defer_ms);

	registerEvent(checkDeferEvent, 750);

	registerEvent(checkAgentEvent, 0);
	registerEvent(checkClientEvent, 0);
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
