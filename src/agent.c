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

#include "agent.h"
#include "logging.h"

void markJobsUnknown(agent *a);
void markQueueStopped(agent *a);

agent * agentList = NULL;

void addAgent(agent * a) {
	if (agentList) {
		a->next = agentList;
		a->next->prev = a;
	}

	agentList = a;
}

void removeAgent(agent * a) {
	if (a->next)
		a->next->prev = a->prev;

	if (a->prev)
		a->prev->next = a->next;

	if (a == agentList)
		agentList = a->next;
}

int handleAgentConnection(struct connectionType * conn) {
	int agent_fd;

	print_msg(JERS_LOG_DEBUG, "Agent has connected");

	agent_fd = _accept(conn->socket);

	if (agent_fd < 0) {
		print_msg(JERS_LOG_INFO, "accept() failed for agent: %s", strerror(errno));
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
	for (a = agentList; a; a = a->next) {
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
			a->connection.event_fd = conn->event_fd;
			a->connection.events = 0;
			a->logged_in = 0;

			pollSetReadable(&a->connection);
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

int handleAgentDisconnect(agent * a) {
	print_msg(JERS_LOG_CRITICAL, "JERS AGENT DISCONNECTED: %s", a->host);

	/* Stop receiving events for this socket */
	if (pollRemoveSocket(&a->connection) != 0)
		fprintf(stderr, "Failed deleting event for agent socket : %s\n", strerror(errno));

	/* Mark jobs on this agent as unknown */
	markJobsUnknown(a);
	
	/* Disable queues for this agent */
	markQueueStopped(a);

	close(a->connection.socket);
	a->connection.socket = -1;
	a->connection.events = 0;
	a->logged_in = 0;
	free(a->nonce);
	a->nonce = NULL;

	buffFree(&a->requests);
	buffFree(&a->responses);
	return 0;
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

int handleAgentWrite(agent * a) {
	int len = 0;

	len = _send(a->connection.socket, a->responses.data + a->sent, a->responses.used - a->sent);

	if (len == -1) {
		print_msg(JERS_LOG_WARNING, "send to agent failed: %s", strerror(errno));
		return 1;
	}

	a->sent += len;

	/* Sent everything, remove EPOLLOUT */
	if (a->sent == a->responses.used) {
		a->responses.used = 0;
		a->sent = 0;
		pollSetReadable(&a->connection);
	}

	return 0;
}
