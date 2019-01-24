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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "server.h"
#include "comms.h"
#include "resp.h"

static ssize_t _recv(int fd, void * buf, size_t count) {
	ssize_t len;

	while (1) {
		len = recv(fd, buf, count, 0);

		if (len == -1 && errno == EINTR)
				continue;

		break;
	}

	return len;
}

static ssize_t _send(int fd, const void * buf, size_t count) {
	ssize_t len;

	while (1) {
		len = send(fd, buf, count, 0);

		if (len == -1 && errno == EINTR)
				continue;

		break;
	}

	return len;
}

/* Set EPOLLIN | EPOLLOUT as the connections registered events */
int setWritable(struct connectionType * connection) {
	struct epoll_event ee;

	int action = connection->events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
	ee.events = connection->events | EPOLLOUT;
	ee.data.ptr = connection;

	if (epoll_ctl(server.event_fd, action, connection->socket, &ee)) {
		return -1;
	}

	connection->events |= EPOLLOUT;

	return 0;
}

/* Set EPOLLIN as the connections registered event */
int setReadable(struct connectionType * connection) {
	struct epoll_event ee;

	int action = connection->events == 0 ? EPOLL_CTL_ADD: EPOLL_CTL_MOD;

	ee.events = EPOLLIN;
	ee.data.ptr = connection;

	if (epoll_ctl(server.event_fd, action, connection->socket, &ee)) {
		return -1;
	}

	connection->events |= EPOLLIN;

	return 0;
}

void setup_listening_sockets(void) {
	struct sockaddr_un addr;
	int fd = -1;

	fd = socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, 0);

	if (fd == -1) {
		error_die("failed to create listening socket %s", strerror(errno));
	}

 	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, server.socket_path, sizeof(addr.sun_path)-1);

	unlink(server.socket_path);

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("bind error");
		exit(-1);
	}

	if (listen(fd, 1024) == -1) {
		perror("listen error");
		exit(-1);
	}

	/* Permissions are controlled by the daemon, when a request is made.
	 * This means the permissions on the socket are fairly open.
	 * This should be changed at some point to have the permissions/owner/group configurable via the config file */
	if (chmod(server.socket_path, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) != 0)
		error_die("chmod failed on listening socket %s", strerror(errno));

	server.client_connection.type = CLIENT_CONN;
	server.client_connection.socket = fd;
	server.client_connection.ptr = NULL;
	server.client_connection.events = 0;

	setReadable(&server.client_connection);

	/* Agent socket */
	fd = socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, 0);

	if (fd == -1) {
		error_die("failed to create listening socket %s", strerror(errno));
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, server.agent_socket_path, sizeof(addr.sun_path)-1);

	if (unlink(server.agent_socket_path) && errno != ENOENT) {
		error_die("failed to unlink daemon socket: %s\n", strerror(errno));
	}

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("bind error");
		exit(-1);
	}

	if (listen(fd, 1024) == -1) {
		perror("listen error");
		exit(-1);
	}

	server.agent_connection.type = AGENT_CONN;
	server.agent_connection.socket = fd;
	server.agent_connection.ptr = NULL;
	server.agent_connection.events = 0;

	setReadable(&server.agent_connection);
}

void handleClientDisconnect(client * c) {
	/* Stop receiving events for this socket */
	struct epoll_event ee;

	if (epoll_ctl(server.event_fd, EPOLL_CTL_DEL, c->connection.socket, &ee)) {
		fprintf(stderr, "epoll_ctl failed deleting event for client");
	}

	close(c->connection.socket);
	buffFree(&c->response);
	buffFree(&c->request);
	respReadFree(&c->msg.reader);
	removeClient(c);
	free(c);

	return;
}

/* Accept a new client connection, adding to our existing list of clients and adding it
 * to our event polling */

void handleClientConnection(void) {
	client * c = NULL;
	int client_fd = -1;

	while (1) {
		client_fd = accept(server.client_connection.socket, NULL, NULL);

		if (client_fd < 0) {
			if (errno == EINTR)
				continue;
			print_msg(JERS_LOG_INFO, "accept() failed: %s", strerror(errno));
			return;
		}

		break;
	}

	/* Set some flags on the clients socket */
	int flags = fcntl(client_fd, F_GETFL, 0);
	if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		print_msg(JERS_LOG_WARNING, "failed to set client socket as nonblocking");
		close(client_fd);
		return;
	}

	c = calloc(sizeof(client), 1);

	if (!c) {
		error_die("Failed to malloc memory for client: %s\n", strerror(errno));
	}

	/* Get the client UID off the socket */
	struct ucred creds;
	socklen_t len = sizeof(struct ucred);

	if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &creds, &len) == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to get peercred from client connection: %s", strerror(errno));
		print_msg(JERS_LOG_WARNING, "Closing connection to client");
		close(client_fd);
		free(c);
		return;
	}

	c->uid = creds.uid;
	c->connection.type = CLIENT;
	c->connection.ptr = c;
	c->connection.socket = client_fd;
	c->connection.events = 0;

	buffNew(&c->request, 0);

	addClient(c);

	/* Add this client to our event polling */
	if (setReadable(&c->connection) != 0) {
		print_msg(JERS_LOG_WARNING, "Failed to set client as readable: %s", strerror(errno));
		handleClientDisconnect(c);
		return;
	}

	return;
}

void handleAgentConnection(void) {
	int agent_fd;
	agent * a = NULL;

	print_msg(JERS_LOG_INFO, "Agent has connected");

	while (1) {
		agent_fd = accept(server.agent_connection.socket, NULL, NULL);

		if (agent_fd < 0) {
			if (errno == EINTR)
				continue;
			print_msg(JERS_LOG_INFO, "accept() failed for agent: %s", strerror(errno));
			return;
		}

		break;
	}

	/* Set some flags on the socket */
	int flags = fcntl(agent_fd, F_GETFL, 0);
	if (fcntl(agent_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		print_msg(JERS_LOG_WARNING, "failed to set agent socket as nonblocking");
		close(agent_fd);
		return;
	}

	a = calloc(sizeof(agent), 1);

	a->connection.type = AGENT;
	a->connection.ptr = a;
	a->connection.socket = agent_fd;
	a->connection.events = 0;

	setReadable(&a->connection);

	buffNew(&a->requests, 0);
	buffNew(&a->responses, 0);
	a->sent = 0;

	/* Add it to our list of agent */
	addAgent(a);

	print_msg(JERS_LOG_INFO, "New agent connection initalised");

	return;
}

/* The agent has disappeared.
 * - Mark any jobs that were in running as 'Unknown'. There is a chance they will reconnect.
 */

void handleAgentDisconnect(agent * a) {
	print_msg(JERS_LOG_CRITICAL, "JERS AGENT DISCONNECTED.");

	/* Stop receiving events for this socket */
	struct epoll_event ee;
	if (epoll_ctl(server.event_fd, EPOLL_CTL_DEL, a->connection.socket, &ee)) {
		fprintf(stderr, "epoll_ctl failed deleting event for agent");
	}

	/* Mark jobs on this agent as unknown */
	struct job * j;

	for (j = server.jobTable; j != NULL; j = j->hh.next) {
		if (j->queue->agent == a && (j->state & JERS_JOB_RUNNING || j->internal_state & JERS_JOB_FLAG_STARTED)) {
			print_msg(JERS_LOG_WARNING, "Setting JobId %d to unknown", j->jobid);
			j->internal_state = 0;
			
		}
	}

	/* Disable queues for this agent */
	struct queue * q;
	for (q = server.queueTable; q != NULL; q = q->hh.next) {
		if (q->agent == a) {
			q->agent = NULL;
			print_msg(JERS_LOG_DEBUG, "Disabling queue %s", q->name);

			q->state &= ~JERS_QUEUE_FLAG_STARTED;
		}
	}

	close(a->connection.socket);
	buffFree(&a->requests);
	buffFree(&a->responses);
	respReadFree(&a->msg.reader);
	free(a->host);

	removeAgent(a);
	free(a);
}

/* Handle read activity on a agent socket */
void handleAgentRead(agent * a) {
	int len = 0;

	buffResize(&a->requests, 0);

	len = _recv(a->connection.socket, a->requests.data + a->requests.used, a->requests.size - a->requests.used);

	if (len < 0) {
 		if ((errno == EAGAIN || errno == EWOULDBLOCK))
			return;

		print_msg(JERS_LOG_WARNING, "failed to read from agent: %s\n", strerror(errno));
		handleAgentDisconnect(a);
		return;
	} else if (len == 0) {
		/* Disconnected */
		handleAgentDisconnect(a);
		return;
	}

	a->requests.used += len;
}

/* Handle read activity on a client socket */
void handleClientRead(client * c) {
	int len = 0;

	buffResize(&c->request, 0);

	len = _recv(c->connection.socket, c->request.data + c->request.used, c->request.size - c->request.used);
	if (len < 0) {
 		if ((errno == EAGAIN || errno == EWOULDBLOCK))
			return;

		print_msg(JERS_LOG_WARNING, "failed to read from client: %s\n", strerror(errno));
		handleClientDisconnect(c);
		return;
	} else if (len == 0) {
		/* Disconnected */
		handleClientDisconnect(c);
		return;
	}

	/* Got some data from the client.
	 * Update the buffer and length in the
	 * reader, so we can try to parse this request */
	c->request.used += len;
}

void handleAgentWrite(agent * a) {
	int len = 0;

	len = _send(a->connection.socket, a->responses.data + a->sent, a->responses.used - a->sent );

	if (len == -1) {
		print_msg(JERS_LOG_WARNING, "send to agent failed: %s", strerror(errno));
		return;
	}

	a->sent += len;

	/* Sent everything, remove EPOLLOUT */
	if (a->sent == a->responses.used) {
		setReadable(&a->connection);
	}
}
void handleClientWrite(client * c) {
	int len = 0;

	len = _send(c->connection.socket, c->response.data + c->response_sent, c->response.used - c->response_sent);

	if (len == -1) {
		print_msg(JERS_LOG_WARNING, "send to client failed: %s", strerror(errno));
		//TODO: free client
		return;
	}

	c->response_sent += len;

	/* If we have sent all out data, remove EPOLLOUT
	 * from the event. Leave readable on, as we might read another request
	 * from the client, or process their disconnect */
	if (c->response_sent == c->response.used) {
		setReadable(&c->connection);
		c->response_sent = c->response.used = 0;
	}
}

/* A readable event could be a connection from a client/agent,
 * Data to read, or a disconnection */

void handleReadable(struct epoll_event * e) {
	struct connectionType * connection = e->data.ptr;

	switch (connection->type) {
		case CLIENT_CONN: handleClientConnection(); break;
		case AGENT_CONN:  handleAgentConnection(); break;
		case CLIENT:      handleClientRead(connection->ptr); break;
		case AGENT:       handleAgentRead(connection->ptr); break;
		default:          print_msg(JERS_LOG_WARNING, "Unexpected read event - Ignoring"); break;
	}

	return;
}

/* A writeable event flags we can send more data to the client/agent */

void handleWriteable(struct epoll_event * e) {
	struct connectionType * connection = e->data.ptr;

	switch (connection->type) {
		case CLIENT: handleClientWrite(connection->ptr); break;
		case AGENT:  handleAgentWrite(connection->ptr); break;
		default:     print_msg(JERS_LOG_WARNING, "Unexpected write event - Ignoring"); break;
	}

	return;
}
