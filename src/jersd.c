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
#include <sys/stat.h>

#ifdef USE_SYSTEMD
#include "systemd/sd-daemon.h"
#endif

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
		else if (strcasecmp("--no-save", argv[i]) == 0)
			server.nosave = 1;
	}

	return 0;
}

void serverShutdown(void) {
	/* Lets do a final flush of our state file before we try anything else*/
	if (server.journal.fd >= 0) {
		print_msg(JERS_LOG_INFO, "Performing final flush of state file");
		fdatasync(server.journal.fd);
	}

	/* Kill off any accounting stream clients */
	acctClient *ac = acctClientList;
	while (ac) {
		acctClient *next = ac->next;

		print_msg(JERS_LOG_INFO, "Sending SIGTERM in acct stream pid:%d", ac->pid);
		kill(ac->pid, SIGTERM);
		close(ac->connection.socket);
		removeAcctClient(ac);
		free(ac);

		ac = next;
	}

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

	/* Clients and agents */
	client *c = clientList;
	while (c) {
		client *next = c->next;

		close(c->connection.socket);
		buffFree(&c->response);
		buffFree(&c->request);
		removeClient(c);
		free(c);

		c = next;
	}

	agent *a = agentList;
	while (a) {
		agent *next = a->next;

		close(a->connection.socket);
		buffFree(&a->requests);
		buffFree(&a->responses);
		free(a->host);
		free(a->nonce);
		removeAgent(a);
		free(a);

		a = next;
	}

	/* Commands and fields */
	freeSortedCommands();
	freeSortedFields();

	freeEvents();
	freeConfig();
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

void setup_listening_sockets(void) {

	int fd = createSocket(server.socket_path, 0, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

	if (fd < 0)
		error_die("Failed to create listening socket for client connections: %s", strerror(errno));

	server.client_connection.type = CLIENT_CONN;
	server.client_connection.socket = fd;
	server.client_connection.event_fd = server.event_fd;
	server.client_connection.ptr = NULL;
	server.client_connection.events = 0;

	pollSetReadable(&server.client_connection);

	/* Agent connections over TCP */
	if (server.agent_port) {
		fd = createSocket(NULL, server.agent_port, 0);

		if (fd == -1)
			error_die("failed to create listening socket for port %d: %s", server.agent_port, strerror(errno));

		server.agent_connection_tcp.type = AGENT_CONN;
		server.agent_connection_tcp.socket = fd;
		server.agent_connection_tcp.event_fd = server.event_fd;
		server.agent_connection_tcp.ptr = NULL;
		server.agent_connection_tcp.events = 0;

		pollSetReadable(&server.agent_connection_tcp);
	}

	/* Agent socket */
	fd = createSocket(server.agent_socket_path, 0, 0);

	if (fd == -1) {
		error_die("failed to create listening socket %s", strerror(errno));
	}

	server.agent_connection.type = AGENT_CONN;
	server.agent_connection.socket = fd;
	server.agent_connection.event_fd = server.event_fd;
	server.agent_connection.ptr = NULL;
	server.agent_connection.events = 0;

	pollSetReadable(&server.agent_connection);

	/* Accounting stream socket */
	fd = createSocket(server.acct_socket_path, 0, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

	server.acct_connection.type = ACCT_CONN;
	server.acct_connection.socket = fd;
	server.acct_connection.event_fd = server.event_fd;
	server.acct_connection.ptr = NULL;
	server.acct_connection.events = 0;

	pollSetReadable(&server.acct_connection);
}

void handleReadable(struct epoll_event *e) {
	struct connectionType * connection = e->data.ptr;
	int status = 0;

	switch (connection->type) {
		case CLIENT_CONN:       status = handleClientConnection(connection); break;
		case AGENT_CONN:        status = handleAgentConnection(connection); break;
		case ACCT_CONN:         status = handleAcctClientConnection(connection); break;
		case CLIENT:            status = handleClientRead(connection->ptr); break;
		case AGENT:             status = handleAgentRead(connection->ptr); break;
		default:                print_msg(JERS_LOG_WARNING, "Unexpected read event - Ignoring"); break;
	}

	if (status) {
		/* Disconnected */
		e->data.ptr = NULL;
	}

	return;
}

void handleWriteable(struct epoll_event *e) {
	struct connectionType * connection = e->data.ptr;

	/* Disconnected before handling the write event */
	if (connection == NULL)
		return;

	switch (connection->type) {
		case CLIENT:       handleClientWrite(connection->ptr); break;
		case AGENT:        handleAgentWrite(connection->ptr); break;
		default:           print_msg(JERS_LOG_WARNING, "Unexpected write event - Ignoring"); break;
	}

	return;
}

int main (int argc, char * argv[]) {

	memset(&server, 0, sizeof(struct jersServer));

	if (parseOpts(argc, argv))
		error_die("Argument parsing failed\n");

	if (server.daemon)
		setupAsDaemon();

	setup_handlers(shutdownHandler);
	loadConfig(server.config_file);

#ifdef INIT_SETPROCTITLE_REPLACEMENT
    spt_init(argc, argv);
#endif

	server_log_mode = server.logging_mode;

	print_msg(JERS_LOG_INFO, "\n"
			"     _  ___  ___  ___                   \n"
			"  _ | || __|| _ \\/ __|                  \n"
			" | || || _| |   /\\__ \\                  \n"
			"  \\__/ |___||_|_\\|___/  Version %d.%d.%d\n\n", JERS_MAJOR, JERS_MINOR, JERS_PATCH);

	print_msg(JERS_LOG_INFO, "jersd v%d.%d.%d starting.", JERS_MAJOR, JERS_MINOR, JERS_PATCH);

	server.journal.fd = -1;
	server.agent_connection.socket = -1;
	server.client_connection.socket = -1;
	server.initalising = 1;

	/* Perform some sorts for improved lookup performance */
	sortfields();
	sortAgentCommands();
	sortCommands();

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
	server.initalising = 0;

	print_msg(JERS_LOG_INFO, "* JERSD entering main loop...\n");

#ifdef USE_SYSTEMD
	/* Signal to systemd we are ready to process requests */
	sd_notify(0, "READY=1");
	sd_notify(0, "STATUS=Ready for requests");
#endif

	/* Away we go. We will sit in this loop until a shutdown is requested */
	while (1) {

		if (server.shutdown) {
			print_msg(JERS_LOG_INFO, "Shutdown requested via signal");
#ifdef USE_SYSTEMD
			sd_notify(0, "STOPPING=1");
#endif
			break;
		}

		/* Poll for any events on our sockets */
		int status = epoll_wait(server.event_fd, events, MAX_EVENTS, server.event_freq);

		for (int i = 0; i < status; i++) {
			struct epoll_event * e = &events[i];

			/* The socket is readable */
			if (e->events &EPOLLIN)
				handleReadable(e);

			/* Socket is writeable */
			if (e->events &EPOLLOUT)
				handleWriteable(e);
		}

		/* Check for any expired events to check */
		checkEvents();
	}

	print_msg(JERS_LOG_INFO, "Exited main loop - Shutting down.\n");

	free(events);
	serverShutdown();

	return 0;
}
