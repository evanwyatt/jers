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

	/* Agent connections over TCP */
	if (server.agent_port) {
		fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);

		if (fd == -1)
			error_die("failed to create listening socket for port %d: %s", server.agent_port, strerror(errno));

		struct sockaddr_in servaddr;
		memset(&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family  = AF_INET;
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
		servaddr.sin_port = htons(server.agent_port); 

		if ((bind(fd, &servaddr, sizeof(servaddr))) != 0)
			error_die("Failed to bind socket to TCP port %d: %s", server.agent_port, strerror(errno));

		if (listen(fd, 1024) == -1)
			error_die("Failed to listen on TCP port %d: %s", server.agent_port, strerror(errno));

		server.agent_connection_tcp.type = AGENT_CONN;
		server.agent_connection_tcp.socket = fd;
		server.agent_connection_tcp.ptr = NULL;
		server.agent_connection_tcp.events = 0;

		setReadable(&server.agent_connection_tcp);
	}

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

int handleClientConnection(void) {
	client * c = NULL;
	int client_fd = -1;

	while (1) {
		client_fd = accept(server.client_connection.socket, NULL, NULL);

		if (client_fd < 0) {
			if (errno == EINTR)
				continue;
			print_msg(JERS_LOG_INFO, "accept() failed: %s", strerror(errno));
			return 1;
		}

		break;
	}

	/* Set some flags on the clients socket */
	int flags = fcntl(client_fd, F_GETFL, 0);
	if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		print_msg(JERS_LOG_WARNING, "failed to set client socket as nonblocking");
		close(client_fd);
		return 1;
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
		return 1;
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
		return 1;
	}

	return 0;
}

int handleAgentConnection(struct connectionType * conn) {
	int agent_fd;

	print_msg(JERS_LOG_DEBUG, "Agent has connected");

	while (1) {
		agent_fd = accept(conn->socket, NULL, NULL);

		if (agent_fd < 0) {
			if (errno == EINTR)
				continue;
			print_msg(JERS_LOG_INFO, "accept() failed for agent: %s", strerror(errno));
			return 1;
		}

		break;
	}

	/* Set some flags on the socket */
	int flags = fcntl(agent_fd, F_GETFL, 0);
	if (fcntl(agent_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		print_msg(JERS_LOG_WARNING, "failed to set agent socket as nonblocking");
		close(agent_fd);
		return 1;
	}

	/* Get the hosts details */
	struct sockaddr_in peerAddr;
	socklen_t len = sizeof(peerAddr);
	if (getpeername(agent_fd, &peerAddr, &len) != 0) {
		print_msg(JERS_LOG_WARNING, "Failed to getpeername of connecting agent: %s", strerror(errno));
		close(agent_fd);
		return 1;
	}

	/* We expect the agent to be resolvable to a single hostname, ie. It has one PTR record. */
	char host[NI_MAXHOST];
	int status;
	if ((status = getnameinfo((struct sockaddr *)&peerAddr, sizeof(peerAddr), host, sizeof(host), NULL, 0, NI_NAMEREQD)) != 0) {
		print_msg(JERS_LOG_WARNING, "Failed to get name of peer getnameinfo(): %s", gai_strerror(status));
		close(agent_fd);
		return 1;
	}

	print_msg(JERS_LOG_INFO, "Got agent connection attempt from host: %s", host);

	/* Check if this agent is allowed to connect to us. */
	agent *a = NULL;
	for (a = server.agent_list; a; a = a->next) {
		if (strcmp(a->host, host) == 0) {
			/* Found it */
			if (a->connection.socket >= 0) {
				print_msg(JERS_LOG_CRITICAL, "An agent connection attempt was made from %s, but they are were already connected.", host);
				close(agent_fd);
				return 1;
			}

			a->connection.type = AGENT;
			a->connection.ptr = a;
			a->connection.socket = agent_fd;
			a->connection.events = 0;
			a->logged_in = 0;

			setReadable(&a->connection);
			buffNew(&a->requests, 0);
			buffNew(&a->responses, 0);
			a->sent = 0;

			print_msg(JERS_LOG_INFO, "New agent connection from '%s' initalised", host);
			break;
		}
	}

	if (a == NULL) {
		print_msg(JERS_LOG_WARNING, "Connecting agent from '%s' not in allowed list. Disconnecting them.", host);
		close(agent_fd);
		return 1;
	}

	return 0;
}

/* The agent has disappeared.
 * - Mark any jobs that were in running as 'Unknown'. There is a chance the agent will reconnect and update the status. */

void handleAgentDisconnect(agent * a) {
	print_msg(JERS_LOG_CRITICAL, "JERS AGENT DISCONNECTED: %s", a->host);

	/* Stop receiving events for this socket */
	struct epoll_event ee;
	if (epoll_ctl(server.event_fd, EPOLL_CTL_DEL, a->connection.socket, &ee)) {
		fprintf(stderr, "epoll_ctl failed deleting event for agent");
	}

	/* Mark jobs on this agent as unknown */
	for (struct job *j = server.jobTable; j != NULL; j = j->hh.next) {
		if (j->queue->agent == a && (j->state & JERS_JOB_RUNNING || j->internal_state & JERS_FLAG_JOB_STARTED)) {
			print_msg(JERS_LOG_WARNING, "Job %d is now unknown", j->jobid);
			j->internal_state = 0;
			changeJobState(j, JERS_JOB_UNKNOWN, NULL, 1);
		}
	}

	/* Disable queues for this agent */
	for (struct queue *q = server.queueTable; q != NULL; q = q->hh.next) {
		if (q->agent == a) {
			print_msg(JERS_LOG_DEBUG, "Disabling queue %s", q->name);
			q->agent = NULL;
			q->state &= ~JERS_QUEUE_FLAG_STARTED;
		}
	}

	close(a->connection.socket);
	a->connection.socket = -1;
	a->connection.events = 0;
	a->logged_in = 0;
	free(a->nonce);
	a->nonce = NULL;

	buffFree(&a->requests);
	buffFree(&a->responses);
	respReadFree(&a->msg.reader);
}

/* Handle read activity on a agent socket */
int handleAgentRead(agent * a) {
	int len = 0;

	buffResize(&a->requests, 0);

	len = _recv(a->connection.socket, a->requests.data + a->requests.used, a->requests.size - a->requests.used);

	if (len < 0) {
 		if ((errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;

		print_msg(JERS_LOG_WARNING, "failed to read from agent: %s\n", strerror(errno));
		handleAgentDisconnect(a);
		return 1;
	} else if (len == 0) {
		/* Disconnected */
		handleAgentDisconnect(a);
		return 1;
	}

	a->requests.used += len;

	return 0;
}

/* Handle read activity on a client socket */
int handleClientRead(client * c) {
	int len = 0;

	buffResize(&c->request, 0);

	len = _recv(c->connection.socket, c->request.data + c->request.used, c->request.size - c->request.used);
	if (len < 0) {
 		if ((errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;

		print_msg(JERS_LOG_WARNING, "failed to read from client: %s\n", strerror(errno));
		handleClientDisconnect(c);
		return 1;
	} else if (len == 0) {
		/* Disconnected */
		handleClientDisconnect(c);
		return 1;
	}

	/* Got some data from the client.
	 * Update the buffer and length in the
	 * reader, so we can try to parse this request */
	c->request.used += len;

	return 0;
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
int handleClientWrite(client * c) {
	int len = 0;

	len = _send(c->connection.socket, c->response.data + c->response_sent, c->response.used - c->response_sent);

	if (len == -1) {
		print_msg(JERS_LOG_WARNING, "send to client failed: %s", strerror(errno));
		handleClientDisconnect(c);
		return 1;
	}

	c->response_sent += len;

	/* If we have sent all our data, remove EPOLLOUT
	 * from the event. Leave readable on, as we might read another request
	 * from the client, or process their disconnect */
	if (c->response_sent == c->response.used) {
		setReadable(&c->connection);
		c->response_sent = c->response.used = 0;
	}

	return 0;
}

/* A readable event could be a connection from a client/agent,
 * Data to read, or a disconnection */

void handleReadable(struct epoll_event * e) {
	struct connectionType * connection = e->data.ptr;
	int status = 0;

	switch (connection->type) {
		case CLIENT_CONN: status = handleClientConnection(); break;
		case AGENT_CONN:  status = handleAgentConnection(connection); break;
		case CLIENT:      status = handleClientRead(connection->ptr); break;
		case AGENT:       status = handleAgentRead(connection->ptr); break;
		default:          print_msg(JERS_LOG_WARNING, "Unexpected read event - Ignoring"); break;
	}

	if (status) {
		/* Disconnected */
		e->data.ptr = NULL;
	}

	return;
}

/* A writeable event flags we can send more data to the client/agent */

void handleWriteable(struct epoll_event * e) {
	struct connectionType * connection = e->data.ptr;

	/* Disconnected before handling the write event */
	if (connection == NULL)
		return;

	switch (connection->type) {
		case CLIENT: handleClientWrite(connection->ptr); break;
		case AGENT:  handleAgentWrite(connection->ptr); break;
		default:     print_msg(JERS_LOG_WARNING, "Unexpected write event - Ignoring"); break;
	}

	return;
}
