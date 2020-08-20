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
#include <logging.h>
#include <acct.h>
#include <json.h>

#include <ctype.h>
#include <glob.h>

int jobToJSON(struct job *j, buff_t *buf);
int queueToJSON(struct queue *q, buff_t *buf);
int resourceToJSON(struct resource *r, buff_t *buf);

acctClient *acctClientList = NULL;

static void acctMain(acctClient *a);

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
	acctMain(a);

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
	for(struct queue *q = server.queueTable; q; q = q->hh.next)
		queueToJSON(q, &a->response);

	for(struct resource *r = server.resTable; r; r = r->hh.next)
		resourceToJSON(r, &a->response);

	for(struct job *j = server.jobTable; j; j = j->hh.next)
		jobToJSON(j, &a->response);

	/* Send a 'stream-start' message */
	buff_t b;
	buffNew(&b, 0);

	JSONStart(&b);
	JSONStartObject(&b, "STREAM_START", 12);

	asprintf(&a->id, "%s:%ld", server.journal.datetime, server.journal.record);

	JSONAddString(&b, ACCT_ID, a->id);

	JSONEndObject(&b);
	JSONEnd(&b);

	buffAddBuff(&a->response, &b);
	buffFree(&b);

	pollSetWritable(&a->connection);

	return 0;
}

/* Locate to 'id' */
int locateJournal(acctClient *a, char *id) {
	print_msg_debug("LOCATING Journal to : %s\n", id);
	/* Break up and validate the id */
	char *sep = strchr(id, ':');

	if (sep == NULL)
		return 1;

	*sep = '\0';
	sep++;

	if (strlen(id) > 8) //YYYYMMDD
		return 1;

	strcpy(a->datetime, id);

	while(*id) {
		if (!isdigit(*id))
			return 1;

		id++;
	}

	a->record = atol(sep);

	char journal[PATH_MAX];
	sprintf(journal, "%s/journal.%s", server.state_dir, a->datetime);

	a->journal = fopen(journal, "rb");

	if (a->journal == NULL)
		error_die("Failed to open journal file '%s': %s", journal, strerror(errno));

	/* Now locate to the requested record */
	char *record = NULL;
	size_t record_size = 0;
	ssize_t record_len = 0;

	off_t current = 0;

	while ((record_len = getline(&record, &record_size, a->journal)) != -1) {
		if (*record == '\0')
			break;

		if (++current == a->record)
			break;
	}

	free(record);

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

			/* Locate onto the provided position */
			locateJournal(a, a->id);
		} else {
			if (a->initalised == 0) {
				/* Need to send them the current list of jobs/queue/resources */
				sendInitial(a);
				a->initalised = 1;

				asprintf(&a->id, "%s:%ld", server.journal.datetime, server.journal.record);
				locateJournal(a, a->id);
			}
		}

		a->state = ACCT_STARTED;

	} else if (strcasecmp(cmd, "STOP") == 0) {
		a->state = ACCT_STOPPED;
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
static void acctMain(acctClient *a) {
	setproctitle("jersd_acct[%d]", a->connection.socket);
	buff_t b;

	char *record = NULL;
	size_t record_size = 0;
	ssize_t record_len = 0;
	char id[32];
	off_t current_pos = 0;

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

	/* Buffer to read client requests into */
	buffNew(&a->request, 0);

	buffNew(&b, 0);

	/* Main processing loop */
	while (1) {
		if (shutdown_flag) {
			print_msg(JERS_LOG_INFO, "Shutdown of accounting stream requested.\n");
			break;
		}

		/* Poll for any events on our sockets */
		int status = epoll_wait(a->connection.event_fd, events, MAX_EVENTS, 2000);

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

		/* Read the current journal, sending any new messages.
		 * We need to also check if we need to open the next journal. */

		current_pos = ftell(a->journal);

		while ((record_len = getline(&record, &record_size, a->journal)) != -1) {
			if (*record == '\0') {
				fseek(a->journal, current_pos, SEEK_SET);

				/* Check if we need to switch to a new journal file */
				/* Get a list of all journal files, find the one we currently have open */
				char glob_pattern[PATH_MAX];
				char current_journal[PATH_MAX];
				sprintf(glob_pattern, "%s/journal.*", server.state_dir);
				sprintf(current_journal, "%s/journal.%s", server.state_dir, a->datetime);

				glob_t glob_buff;

				if (glob(glob_pattern, 0, NULL, &glob_buff) == 0) {
					size_t i = 0;
					for (i = 0; i < glob_buff.gl_pathc; i++) {
						if (strcmp(current_journal, glob_buff.gl_pathv[i]) == 0) {
							i++;
							break;
						}
					}

					if (i < glob_buff.gl_pathc) {
						/* Have a new journal to open */
						FILE *new_journal = fopen(glob_buff.gl_pathv[i], "rb");

						if (new_journal == NULL)
							error_die("Failed to open journal file '%s': %s", glob_buff.gl_pathv[i], strerror(errno));

						/* Opened a new journal. Reset the current stats */
						fclose(a->journal);
						a->record = 0;
						char *dot = strchr(glob_buff.gl_pathv[i], '.');
						dot++;
						strcpy(a->datetime, dot);

						a->journal = new_journal;
						current_pos = 0;

						print_msg_info("Switched to new journal %s\n", a->datetime);
					}

					globfree(&glob_buff);
				}

				break;
			}

			current_pos = ftell(a->journal);

			if (record[record_len - 1] == '\n')
				record[record_len - 1] = '\0';

			a->record++;

			/* Load this message */
			char timestamp[64];
			int64_t revision;
			char command[64];
			uid_t uid;
			jobid_t jobid;
			int msg_offset;

			int field_count = sscanf(record, " %64s\t%d\t%64s\t%u\t%ld\t%n", timestamp, (int *)&uid, command, &jobid, &revision, &msg_offset);

			if (field_count != 5)
				error_die("Failed to load 5 required fields from message");

			if (strcmp(command, "REPLAY_COMPLETE") == 0)
				continue;

			/* Serialize this message */
			JSONStart(&b);
			JSONStartObject(&b, "UPDATE", 6);

			sprintf(id, "%s:%ld", a->datetime, a->record);

			JSONAddString(&b, ACCT_ID, id);
			JSONAddString(&b, TIMESTAMP, timestamp);
			JSONAddString(&b, COMMAND, command);
			JSONAddInt(&b, UID, uid);

			if (jobid)
				JSONAddInt(&b, JOBID, jobid);

			buffAdd(&b, "\"MESSAGE\":", 10);
			buffAdd(&b, record + msg_offset, strlen(record + msg_offset));

			JSONEndObject(&b);
			JSONEnd(&b);

			//fprintf(stderr, "Sending: '%.*s' to accounting stream\n", (int)b.used, b.data);

			buffAddBuff(&a->response, &b);
			buffAdd(&a->response, "\n", 1);

			buffClear(&b, 0);

			pollSetWritable(&a->connection);

			if (shutdown_flag)
				break;
		}
	}

	free(record);
	free(a->id);

	exit(0);
}
