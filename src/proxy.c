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

#include "proxy.h"
#include "logging.h"

proxyClient * proxyClientList = NULL;

void addProxyClient(proxyClient *c) {
	if (proxyClientList) {
		c->next = proxyClientList;
		c->next->prev = c;
	}

	proxyClientList = c;
}

void removeProxyClient(proxyClient * c) {
	if (c->next)
		c->next->prev = c->prev;

	if (c->prev)
		c->prev->next = c->next;

	if (c == proxyClientList)
		proxyClientList = c->next;
}

int handleClientProxyConnection(struct connectionType * connection) {
	int client_fd = _accept(connection->socket);

	if (client_fd < 0) {
		print_msg(JERS_LOG_INFO, "accept() failed during proxy client connect: %s", strerror(errno));
		return 1;
	}

	proxyClient *c = calloc(sizeof(proxyClient), 1);

	if (!c)
		error_die("Failed to malloc memory for proxy client: %s\n", strerror(errno));

	/* Get the client UID off the socket */
	struct ucred creds;
	socklen_t len = sizeof(struct ucred);

	if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &creds, &len) == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to get peercred from proxy client connection: %s", strerror(errno));
		print_msg(JERS_LOG_WARNING, "Closing connection to proxy client");
		close(client_fd);
		free(c);
		return 1;
	}

	c->uid = creds.uid;
	c->pid = creds.pid;
	c->connection.type = CLIENT_PROXY;
	c->connection.ptr = c;
	c->connection.socket = client_fd;
	c->connection.event_fd = connection->event_fd;
	c->connection.events = 0;

	buffNew(&c->request, 0);

	addProxyClient(c);

	/* Add this client to our event polling */
	if (pollSetReadable(&c->connection) != 0) {
		print_msg(JERS_LOG_WARNING, "Failed to set client as readable: %s", strerror(errno));
		handleClientProxyDisconnect(c);
		return 1;
	}

	return 0;
}

int handleClientProxyRead(proxyClient *c) {
	int len = 0;

	buffResize(&c->request, 0);

	len = _recv(c->connection.socket, c->request.data + c->request.used, c->request.size - c->request.used);
	if (len < 0) {
		if ((errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;

		print_msg(JERS_LOG_WARNING, "failed to read from proxy client: %s\n", strerror(errno));
		handleClientProxyDisconnect(c);
		return 1;
	} else if (len == 0) {
		/* Disconnected */
		handleClientProxyDisconnect(c);
		return 1;
	}

	/* Got some data from the client.
	 * Send this to the main daemon process */
	c->request.used += len;

	return 0;
}
int handleClientProxyWrite(proxyClient *c) {
	int len = 0;

	len = _send(c->connection.socket, c->response.data + c->response_sent, c->response.used - c->response_sent);

	if (len == -1) {
		print_msg(JERS_LOG_WARNING, "send to proxy client failed: %s", strerror(errno));
		handleClientProxyDisconnect(c);
		return 1;
	}

	c->response_sent += len;

	/* If we have sent all our data, remove EPOLLOUT
	 * from the event. Leave readable on, as we might read another request
	 * from the client, or process their disconnect */
	if (c->response_sent == c->response.used) {
		pollSetReadable(&c->connection);
		c->response_sent = c->response.used = 0;
	}

	return 0;
}

int handleClientProxyDisconnect(proxyClient *c) {
	/* Stop receiving events for this socket */
	if (pollRemoveSocket(&c->connection) != 0)
		fprintf(stderr, "Failed to remove event for disconnected client");

	close(c->connection.socket);
	buffFree(&c->response);
	buffFree(&c->request);
	respReadFree(&c->msg.reader);

	c->clean_up = 1;

	return 0;
}