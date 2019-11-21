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

#include "client.h"
#include "agent.h"
#include "proxy.h"

ssize_t _recv(int fd, void * buf, size_t count) {
	ssize_t len;

	while (1) {
		len = recv(fd, buf, count, 0);

		if (len == -1 && errno == EINTR)
				continue;

		break;
	}

	return len;
}

ssize_t _send(int fd, const void * buf, size_t count) {
	ssize_t len;

	while (1) {
		len = send(fd, buf, count, 0);

		if (len == -1 && errno == EINTR)
				continue;

		break;
	}

	return len;
}

int _accept(int sockfd) {
	int fd = -1;

	while (1) {
		fd = accept(sockfd, NULL, NULL);

		if (fd == -1 && errno == EINTR)
			continue;

		break;
	}

	/* Make it non-blocking */
	int flags = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		print_msg(JERS_LOG_WARNING, "failed to set socket as nonblocking");
		close(fd);
		return -1;
	}

	return fd;
}

/* Set EPOLLIN | EPOLLOUT as the connections registered events */
int pollSetWritable(struct connectionType * connection) {
	struct epoll_event ee;

	int action = connection->events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
	ee.events = connection->events | EPOLLOUT;
	ee.data.ptr = connection;

	if (epoll_ctl(connection->event_fd, action, connection->socket, &ee)) {
		return -1;
	}

	connection->events |= EPOLLOUT;

	return 0;
}

/* Set EPOLLIN as the connections registered event */
int pollSetReadable(struct connectionType * connection) {
	struct epoll_event ee;

	int action = connection->events == 0 ? EPOLL_CTL_ADD: EPOLL_CTL_MOD;

	ee.events = EPOLLIN;
	ee.data.ptr = connection;

	if (epoll_ctl(connection->event_fd, action, connection->socket, &ee)) {
		return -1;
	}

	connection->events = EPOLLIN;

	return 0;
}

/* Remove a fd from the event_fd */
int pollRemoveSocket(struct connectionType * connection) {
	struct epoll_event ee = {0};

	if (epoll_ctl(connection->event_fd, EPOLL_CTL_DEL, connection->socket, &ee))
		return -1;

	return 0;
}

int createSocket(const char * path, int port, int perm) {
	int fd = -1;
	int enable = 1;
	int domain;

	if (path == NULL && port == 0)
		return -1;

	if (path) {
		print_msg(JERS_LOG_DEBUG, "Creating unix socket: %s", path);
		domain = AF_UNIX;
	}
	else {
		print_msg(JERS_LOG_DEBUG, "Creating socket for port %d", port);
		domain = AF_INET;
	}

	fd = socket(domain, SOCK_STREAM|SOCK_NONBLOCK, 0);

	if (fd == -1)
		return -1;

	if (domain == AF_UNIX) {
		/* UNIX Socket */
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);

		unlink(path);

		if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
			close(fd);
			return -1;
		}

		if (perm && chmod(path, perm) != 0) {
			perror("chmod on path");
			close(fd);
			return -1;
		}
	} else {
		/* TCP */
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
			error_die("Failed to set socket options for socket for port %d: %s", port, strerror(errno));

		struct sockaddr_in servaddr;
		memset(&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family  = AF_INET;
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
		servaddr.sin_port = htons(port); 

		if ((bind(fd, &servaddr, sizeof(servaddr))) != 0)
			error_die("Failed to bind socket to TCP port %d: %s", port, strerror(errno));
	}

	if (listen(fd, 1024) != 0) {
		close(fd);
		return -1;
	}

	return fd;
}
