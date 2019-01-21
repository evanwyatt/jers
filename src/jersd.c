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
#include <unistd.h>
#include <errno.h>
#include <uthash.h>
#include <time.h>
#include <fcntl.h>
#include <stdarg.h>

#include "server.h"
#include "jers.h"
#include "logging.h"

char * server_log = "jersd";
int server_log_mode = JERS_LOG_DEBUG;

struct jersServer server;

int parseOpts(int argc, char * argv[]) {
	for (int i = 0; i < argc; i++) {
		if (strcasecmp("--daemon", argv[i]) == 0)
			server.daemon = 1;
	}

	return 0;
}

void addClient(client * c) {
	if (server.client_list) {
		c->next = server.client_list;
		c->next->prev = c;
	}

	server.client_list = c;
}

void removeClient(client * c) {
	if (c->next) {
		c->next->prev = c->prev;
	}

	if (c->prev) {
		c->prev->next = c->next;
	}
	if (c == server.client_list) {
		server.client_list = c->next;
	}
}

void addAgent(agent * a) {
	if (server.agent_list) {
		a->next = server.agent_list;
		a->next->prev = a;
	}

	server.agent_list = a;
}

void removeAgent(agent * a) {
	if (a->next) {
		a->next->prev = a->prev;
	}

	if (a->prev) {
		a->prev->next = a->next;
	}
	if (a == server.agent_list) {
		server.agent_list = a->next;
	}
}

void serverShutdown(void) {
	/* Close our sockets */
	close(server.client_connection.socket);
	close(server.agent_connection.socket);

	unlink(server.socket_path);
	unlink(server.agent_socket_path);

	/* Free jobs */
	struct job * j, *job_tmp;
	HASH_ITER(hh, server.jobTable, j, job_tmp) {
		HASH_DEL(server.jobTable, j);
		freeJob(j);
	}

	/* Free resources */
	struct resource * r, *res_tmp;
	HASH_ITER(hh, server.resTable, r, res_tmp) {
		HASH_DEL(server.resTable, r);
		freeRes(r);
	}

	/* Free queues */
	struct queue * q, *queue_tmp;
	HASH_ITER(hh, server.queueTable, q, queue_tmp) {
		HASH_DEL(server.queueTable, q);
		freeQueue(q);
	}

	/* User cache */
	freeUserCache();

	/* Scheduling candidate pool */
	free(server.candidate_pool);
}

void shutdownHandler(int signum) {
	if (signum == SIGINT || signum == SIGTERM)
		server.shutdown = 1;
}

void setupAsDaemon(void) {
	/* If we are running as a daemon, we want to redirect
	 * our output to logfile specified in the config file */
	setLogfileName(server_log);
}

int main (int argc, char * argv[]) {

	memset(&server, 0, sizeof(struct jersServer));

	if (parseOpts(argc, argv))
		error_die("Argument parsing failed\n");

	setup_handlers(shutdownHandler);
	loadConfig(server.config_file);

	if (server.daemon)
		setupAsDaemon();

	server_log_mode = server.logging_mode;

	print_msg(JERS_LOG_INFO, "jersd v%s starting.", JERS_VERSION);

	server.state_fd = -1;
	server.agent_connection.socket = -1;
	server.client_connection.socket = -1;

	/* Sort the fields used for serialization/deserialization */
	sortfields();

	stateInit();

	/* Load and initialise the queues */
	if (stateLoadQueues())
		error_die("init: failed to load queues from file");

	if (stateLoadResources())
		error_die("init: failed to load resources from file");

	/* Load jobs from file */

	if (stateLoadJobs())
		error_die("init: failed to load jobs from file");

	/* Replay commands from the journal/s */
	stateReplayJournal();

	print_msg(JERS_LOG_DEBUG, "Initialising sockets\n");

	/* Add server fd to epoll event list */
	server.event_fd = epoll_create(1024);

	if (server.event_fd < 0) {
		error_die("Failed to create epoll fd: %s\n", strerror(errno));
	}

	struct epoll_event * events = malloc(sizeof(struct epoll_event) * MAX_EVENTS);

	setup_listening_sockets();

	/* Start out event polling */
	print_msg(JERS_LOG_DEBUG, "Initialising events\n");
	initEvents();

	server.candidate_recalc = 1;

	print_msg(JERS_LOG_INFO, "* JERSD v%s entering main loop...\n", JERS_VERSION);

	/* Away we go. We will sit in this loop until a shutdown is requested */
	while (1) {

		if (server.shutdown) {
			print_msg(JERS_LOG_INFO, "Shutdown requested via signal");
			break;
		}

		/* Poll for any events on our sockets */
		int status = epoll_wait(server.event_fd, events, MAX_EVENTS, server.event_freq);

		for (int i = 0; i < status; i++) {
			struct epoll_event * e = &events[i];

			/* The socket is readable */
			if (e->events &EPOLLIN)
				handleReadable(e);

			if (e->events &EPOLLOUT)
				handleWriteable(e);
		}

		/* Check for any expired events to check */
		checkEvents();
	}

	print_msg(JERS_LOG_INFO, "Exited main loop - Shutting down.\n");

	free(events);
	serverShutdown();

	/* Lets do a final flush of our state file*/
	if (server.state_fd >= 0) {
		print_msg(JERS_LOG_INFO, "Performing final flush of state file");
		fdatasync(server.state_fd);
	}

	return 0;
}
