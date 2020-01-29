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
#include <acct.h>
#include <json.h>

int jobToJSON(struct job *j, buff_t *buf);
int queueToJSON(struct queue *q, buff_t *buf);
int resourceToJSON(struct resource *r, buff_t *buf);

acctClient *acctClientList = NULL;

static void acctMain(acctClient *a, int journal_fd, off_t stream_start);

void addAcctClient(acctClient *a) {
	if (acctClientList) {
		a->next = acctClientList;
		a->next->prev = a;
	}

	acctClientList = a;
}

void removeAcctClient(acctClient *a) {
	if (a->next)
		a->next->prev = a->prev;

	if (a->prev)
		a->prev->next = a->next;

	if (a == acctClientList)
		acctClientList = a->next;
}

int handleAcctClientConnection(struct connectionType * conn) {
	acctClient *a = NULL;
	int acct_fd = _accept(conn->socket);

	if (acct_fd < 0) {
		print_msg(JERS_LOG_INFO, "accept() failed on accounting connection: %s", strerror(errno));
		return 1;
	}

	a = calloc(sizeof(acctClient), 1);

	if (!a) {
		error_die("Failed to malloc memory for client: %s\n", strerror(errno));
	}

	/* Get the client UID off the socket */
	struct ucred creds;
	socklen_t len = sizeof(struct ucred);

	if (getsockopt(acct_fd, SOL_SOCKET, SO_PEERCRED, &creds, &len) == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to get peercred from accounting connection: %s", strerror(errno));
		print_msg(JERS_LOG_WARNING, "Closing connection to accounting client");
		close(acct_fd);
		free(a);
		return 1;
	}

	a->uid = creds.uid;
	a->connection.type = ACCT_CLIENT;
	a->connection.ptr = a;
	a->connection.socket = acct_fd;
	
	addAcctClient(a);

	/* Fork off to handle this stream
	 * This is done to have a consistent view of the jobs
	 * We also save our current position in the journal so
	 * that we can start streaming of that location */

	print_msg(JERS_LOG_INFO, "Starting accounting stream client for fd:%d uid:%d", a->connection.socket, a->uid);

	off_t stream_start = lseek(server.journal.fd, 0, SEEK_CUR);

	pid_t pid = fork();

	if (pid == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to fork for accounting stream client: %s", strerror(errno));
		close(acct_fd);
		free(a);
		return 1;
	}

	if (pid != 0)
	{
		/* Parent, just return. We will check on this child periodically */
		a->pid = pid;
		return 0;
	}

	/* Child does not return */
	acctMain(a, server.journal.fd, stream_start);

	exit(1);
}

/* Handle read activity on a client socket */
static int handleAcctClientRead(acctClient *a) {
	int len = 0;

	buffResize(&a->request, 0);

	len = _recv(a->connection.socket, a->request.data + a->request.used, a->request.size - a->request.used);
	if (len < 0) {
 		if ((errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;

		print_msg(JERS_LOG_WARNING, "failed to read from accounting client: %s\n", strerror(errno));
		return 1;
	} else if (len == 0) {
		/* Disconnected */
		print_msg(JERS_LOG_WARNING, "Accounting stream client disconnected\n");
		return 1;
	}

	/* Got some data from the client.
	 * Update the buffer and length in the
	 * reader, so we can try to parse this request */
	a->request.used += len;

	return 0;
}

static int handleAcctClientWrite(acctClient * a) {
	int len = 0;

	len = _send(a->connection.socket, a->response.data + a->response_sent, a->response.used - a->response_sent);

	if (len == -1) {
		print_msg(JERS_LOG_WARNING, "send to accounting client failed: %s", strerror(errno));
		return 1;
	}

	a->response_sent += len;

	/* If we have sent all our data, remove EPOLLOUT
	 * from the event. Leave readable on, as we might read another request
	 * from the client, or process their disconnect */
	if (a->response_sent == a->response.used) {
		pollSetReadable(&a->connection);
		a->response_sent = a->response.used = 0;
	}

	return 0;
}

/* Convert all jobs/queues/resources to JSON messages and send them to the client */
static int sendInitial(acctClient *a) {
	for(struct queue *q = server.queueTable; q; q = q->hh.next) {
		queueToJSON(q, &a->response);	
	}

	for(struct resource *r = server.resTable; r; r = r->hh.next) {
		resourceToJSON(r, &a->response);	
	}


	for(struct job *j = server.jobTable; j; j = j->hh.next) {
		jobToJSON(j, &a->response);
	}

	pollSetWritable(&a->connection);

	return 0;
}

static int processRequest(acctClient *a, const char *cmd) {
	print_msg(JERS_LOG_DEBUG, "Got accounting stream cmd: %s\n", cmd);

	if (strncasecmp(cmd, "START", 5) == 0) {
		/* The START command might have an additional parameter */
		if (strlen(cmd) > 6) {
			/* They have provided an ID to use in an acknowledged stream
			 * We need to save this and the journal positions of what we sent them */
			a->id = strdup(cmd + 6);

			if (a->id == NULL) {
				print_msg(JERS_LOG_WARNING, "Failed to allocate ID string for accounting client: %s", strerror(errno));
				return 1;
			}

			if (*a->id == '\0') {
				print_msg(JERS_LOG_WARNING, "No/Invalid ID sent in stream start: %s", cmd);
				return 1;
			}

			/* Load this ID from our state files to determine what to send next */
			//TODO:
			

		} else {
			if (a->initalised == 0) {
				/* Need to send them the current list of jobs/queue/resources */
				sendInitial(a);
				a->initalised = 1;
			}
		}

		a->state = ACCT_STARTED;

	} else if (strcasecmp(cmd, "STOP") == 0) {
		a->state = ACCT_STOPPED;
	} else if (strncasecmp(cmd, "ACK", 3)) {

	} else {
		print_msg(JERS_LOG_WARNING, "Unknown command sent from accounting stream client: %s", cmd);
		return 1;
	}

	return 0;
}

static int checkRequests(acctClient *a) {
	char *start = a->request.data + a->pos;
	char *cmd = start;

	/* Break the request stream by newlines */
	while (start < a->request.data + a->request.used) {

		if (*start == '\n') {
			*start = '\0';
			processRequest(a, cmd);

			cmd = start + 1;
		}

		start++;
	}

	a->pos = cmd - a->request.data;

	return 0;
}

volatile sig_atomic_t shutdown_flag = 0;

static void acctShutdownHandler(int signo) {
	UNUSED(signo);
	shutdown_flag = 1;
	return;
}

/* Does not return */
static void acctMain(acctClient *a, int journal_fd, off_t stream_start) {
	setproctitle("jersd_acct[%d]", a->connection.socket);
	buff_t j, line, temp;
	off_t journal_offset = stream_start;
	char read_buffer[4096];
	int ack = 0;
	int64_t current_id = 0;

	/* Termination handler */
	struct sigaction sigact;

	/* Wrapup & shutdown signals */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = acctShutdownHandler;

	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);

	/* Setup a polling loop to listen for command send from the client */
	a->connection.event_fd = epoll_create(1024);

	if (a->connection.event_fd < 0)
		error_die("Failed to create epoll fd in accounting stream client: %s\n", strerror(errno));

	struct epoll_event *events = malloc(sizeof(struct epoll_event) * MAX_EVENTS);

	if (pollSetReadable(&a->connection) != 0) {
		print_msg(JERS_LOG_WARNING, "Failed to set accounting client as readable: %s", strerror(errno));
		exit(1);
	}

	/* Position ourselves on the next journal message */
	if (lseek(journal_fd, stream_start, SEEK_SET) == -1) {
		print_msg(JERS_LOG_WARNING, "Failed to seek in journal: %s", strerror(errno));
		exit(1);
	}

	buffNew(&a->request, 0);
	buffNew(&j, 0);
	buffNew(&temp, 0);
	buffNew(&line, 256);

	/* Main processing loop */
	while (1) {

		if (shutdown_flag) {
			print_msg(JERS_LOG_INFO, "Shutdown of accounting stream requested.\n");
			break;
		}

		/* Poll for any events on our sockets */
		int status = epoll_wait(a->connection.event_fd, events, MAX_EVENTS, 500);

		for (int i = 0; i < status; i++) {
			struct epoll_event * e = &events[i];

			/* The socket is readable */
			if (e->events &EPOLLIN) {
				if (handleAcctClientRead(a) != 0) {
					print_msg(JERS_LOG_WARNING, "Unable to handle accounting client request.");
					exit(1);
				}
			}

			/* Socket is writeable */
			if (e->events &EPOLLOUT) {
				if (handleAcctClientWrite(a) != 0) {
					print_msg(JERS_LOG_WARNING, "Unable to handle write event.");
					exit(1);
				}
			}
		}

		checkRequests(a);

		if (a->state == ACCT_STOPPED)
			continue;

		/* Check the journal for new messages to send
		 * - We might need to switch journal files if we hit the
		 *   end of journal marker. (This might not have been written
		 *   so we also need to check if a new journal was created) */

		/* Read a chunk of the file in,
		 * We might get a lot of nulls, so just consume everything until we hit a null */
		ssize_t len = pread(journal_fd, read_buffer, sizeof(read_buffer) - 1, journal_offset);

		if (len < 0) {
			print_msg(JERS_LOG_WARNING, "Accounting stream Failed to read journal file: %s\n", strerror(errno));
			exit(1);
		}

		read_buffer[len] = 0;

		if (len <= 0)
			continue;

		/* Get the real length */
		len = strlen(read_buffer);

		if (len <= 0)
			continue;

		/* Advance our read position */
		journal_offset += len;

		ssize_t used = 0;

		/* Copy this new data into our line buffer */
		while (used < len)
		{
			char *nl = strchr(read_buffer + used, '\n');

			if (nl == NULL)
			{
				/* Consumed all the read data */
				if (len - used != 0)
					buffAdd(&temp, read_buffer + used, len - used);

				break;
			}

			/* Null terminate this line */
			*nl = '\0';

			/* Append this to the used buffer */
			size_t line_len = strlen(read_buffer + used);

			buffAdd(&temp, read_buffer + used, line_len);
			used += line_len + 1;

			int field_count = 0;
			int msg_offset = 0;
			struct journal_hdr hdr;

			field_count = sscanf(temp.data, "%c%ld.%d\t%d\t%64s\t%u\t%ld\t%n", &hdr.saved, &hdr.timestamp_s, &hdr.timestamp_ms, (int *)&hdr.uid, hdr.command, &hdr.jobid, &hdr.revision, &msg_offset);

			if (field_count != 7) {
				print_msg(JERS_LOG_CRITICAL, "Failed entry (len:%ld): %s", strlen(temp.data), temp.data);
				error_die("Accounting Stream: Failed to load journal entry. Got %d fields, wanted 6\n", field_count);
			}

			if (*(temp.data + msg_offset)) {
				/* Construct an 'update' message */
				buffClear(&line, line.used);
				JSONStart(&line);
				JSONStartObject(&line, "UPDATE", 6);

				JSONAddInt(&line, DATETIME, hdr.timestamp_s);
				JSONAddInt(&line, UID, hdr.uid);

				if (ack)
					JSONAddInt(&line, ACCT_ID, ++current_id);

				if (hdr.jobid)
					JSONAddInt(&line, JOBID, hdr.jobid);

				buffAdd(&line, "\"COMMAND\":", 10);
				buffAdd(&line, temp.data + msg_offset, strlen(temp.data + msg_offset));

				JSONEndObject(&line);
				JSONEnd(&line);

				fprintf(stderr, "Sending: '%.*s' to accounting stream\n", (int)line.used, line.data);
				buffAddBuff(&a->response, &line);
				pollSetWritable(&a->connection);
			}

			/* Processed a line, clear our the buffer for the next line */
			temp.used = 0;

			if (shutdown_flag)
				break;
		}
	}

	exit(0);
}