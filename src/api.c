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
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>

#include <jers.h>
#include <json.h>
#include <fields.h>
#include <buffer.h>
#include <commands.h>

#define JERS_EXPORT __attribute__((visibility("default")))

#define DEFAULT_CLIENT_TIMEOUT 60 // seconds
#define DEFAULT_REQUEST_SIZE 1024

JERS_EXPORT int jers_errno = JERS_ERR_OK;
char * jers_err_string = NULL;

char * socket_path[3] = {NULL, NULL, NULL};
int fd = -1;
msg_t msg;
buff_t response = {0};
int initalised = 0;

const char * getPendString(int);
const char * getFailString(int);
const char * getErrMsg(int jers_error);
int getJersErrno(char *, char **);

static int jersConnect(void);

static void setJersErrno(int err, char * msg) {
	int saved_errno = errno;
	jers_errno = err;
	
	free(jers_err_string);
	jers_err_string = NULL;

	if (msg)
		jers_err_string = strdup(msg);

	/* Set errno back to what it was before this routine was called */
	errno = saved_errno;
}

static const char * getErrString(int jers_error) {
	if (jers_error < 0 || jers_error > JERS_ERR_UNKNOWN)
		return "Invalid jers_errno provided";

	return jers_err_string ? jers_err_string : getErrMsg(jers_error);
}

static int customInit(const char * custom_config) {
	(void) custom_config;
	return 0;
}

static int defaultInit(void) {
	socket_path[0] = "/run/jers/jers.sock_";
	socket_path[1] = "/run/jers/proxy.sock";

	return 0;
}

JERS_EXPORT int jersInitAPI(const char * custom_config) {
	int rc = 0;

	/* We can be reinitalised by providing a config argument */
	if (initalised && !custom_config) {
		setJersErrno(JERS_ERR_OK, NULL);
		return 0;
	}

	if (custom_config) {
		rc = customInit(custom_config);
	} else {
		rc = defaultInit();
	}

	if (rc) {
		setJersErrno(JERS_ERR_INIT, "Init routine failed");
		return rc;
	}

	if (jersConnect()) {
		setJersErrno(JERS_ERR_INIT, "Failed to connect");
		return 1;
	}

	buffNew(&response, 0);
	initalised = 1;

	return rc;
}

JERS_EXPORT void jersFinish(void) {
	close(fd);
	free_message(&msg);
	buffFree(&response);

	initalised = 0;
	fd = -1;

	setJersErrno(JERS_ERR_OK, NULL);
}

/* Establish a connection to the main daemon */
static int jersConnect(void) {
	struct sockaddr_un addr;
	int status = -1;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (fd < 0) {
		fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
		return 1;
	}

	for (int i = 0; socket_path[i]; i++) {
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;

		strncpy(addr.sun_path, socket_path[i], sizeof(addr.sun_path));
		addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
		printf("Trying: %s\n", socket_path[i]);
		if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
			continue;

		status = 0;
		printf("Connected to: %s\n", socket_path[i]);
		break;
	}

	if (status != 0) {
		fprintf(stderr, "Failed to connect to jers daemon: %s\n", strerror(errno));
		return 1;
	}

	/* Set a recv timeout on the socket */
	struct timeval tv = {DEFAULT_CLIENT_TIMEOUT, 0};

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
		fprintf(stderr, "Failed to set rcvtimeo on socket\n");
		return 1;
	}

	return 0;
}

/* Block until the entire request is sent */
static int sendRequest(buff_t *b) {
	size_t total_sent = 0;
	size_t length;
	char *request;

	JSONEndObject(b);
	JSONEndObject(b);
	JSONEnd(b);

	length = b->used;
	request = b->data;

	while (total_sent < length) {
		ssize_t sent = send(fd, request + total_sent, length - total_sent, MSG_NOSIGNAL);

		if (sent == -1 && errno != EINTR) {
			setJersErrno(JERS_ERR_ESEND, strerror(errno));
			fprintf(stderr, "send to jers daemon failed: %s\n", strerror(errno));
			buffFree(b);
			return 1;
		}

		total_sent += sent;
	}

	buffFree(b);

	return 0;
}

/* Block until we read a full response */
static int readResponse(void) {
	while (1) {
		/* Allocate more memory if we might need it */
		if (buffResize(&response, 0) != 0) {
			setJersErrno(JERS_ERR_MEM, NULL);
			fprintf(stderr, "failed to resize response buffer: %s\n", strerror(errno));
			return 1;
		}

		ssize_t bytes_read = recv(fd, response.data + response.used, response.size - response.used, 0);

		if (bytes_read == -1) {
			if (errno == EINTR)
				continue;

			setJersErrno(JERS_ERR_ERECV, NULL);
			fprintf(stderr, "Error receiving from jers daemon\n");
			return 1;
		} else if (bytes_read == 0) {
			setJersErrno(JERS_ERR_DISCONNECT, NULL);
			fprintf(stderr, "Disconnected from jers daemon\n");
			return 1;
		}
		response.used += bytes_read;

		/* Got a full message yet? */
		char *nl = memchr(response.data, '\n', response.used);

		if (nl == NULL)
			continue;

		/* Try and process the request */
		*nl = '\0';
		nl++;

		if (load_message(response.data, &msg)) {
			setJersErrno(JERS_ERR_INVRESP, NULL);
			fprintf(stderr, "Failed to parse response from jers daemon\n");
			free_message(&msg);
			return 1;
		}

		/* Remove the request from the buffer */
		buffRemove(&response, nl - response.data, 0);

		break;
	}

	/* Check for an error in the response */
	if (msg.error) {
		char * err_msg = NULL;
		int err = getJersErrno(msg.error, &err_msg);

		setJersErrno(err, err_msg);
		free_message(&msg);
		free(err_msg);
		return 1;
	}

	return 0;
}

JERS_EXPORT const char * jersGetErrStr(int jers_error) {
	return getErrString(jers_error);
}

JERS_EXPORT const char * jersGetPendStr(int pend_reason) {
	return getPendString(pend_reason);
}

JERS_EXPORT const char * jersGetFailStr(int fail_reason) {
	return getFailString(fail_reason);
}

JERS_EXPORT void jersFreeJobInfo (jersJobInfo * info) {
	int64_t i, j;

	for (i = 0; i < info->count; i++) {
		jersJob * job = &info->jobs[i];

		free(job->jobname);
		free(job->queue);
		free(job->shell);
		free(job->pre_command);
		free(job->post_command);
		free(job->stdout);
		free(job->stderr);
		free(job->node);

		for (j = 0; j < job->argc; j++)
			free(job->argv[j]);

		free(job->argv);

		for (j = 0; j < job->tag_count; j++) {
			free(job->tags[j].key);
			free(job->tags[j].value);
		}

		free(job->tags);

		for (j = 0; j < job->res_count; j++)
			free(job->resources[j]);

		free(job->resources);
	}

	free(info->jobs);
}

static int deserialize_jersQueue(msg_item * item, jersQueue *q) {
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case QUEUENAME : q->name = getStringField(&item->fields[i]); break;
			case DESC      : q->desc = getStringField(&item->fields[i]); break;
			case NODE      : q->node = getStringField(&item->fields[i]); break;
			case JOBLIMIT  : q->job_limit = getNumberField(&item->fields[i]); break;
			case STATE     : q->state = getNumberField(&item->fields[i]); break;
			case PRIORITY  : q->priority = getNumberField(&item->fields[i]); break;
			case DEFAULT   : q->default_queue = getBoolField(&item->fields[i]); break;

			case STATSRUNNING   : q->stats.running = getNumberField(&item->fields[i]); break;
			case STATSPENDING   : q->stats.pending = getNumberField(&item->fields[i]); break;
			case STATSHOLDING   : q->stats.holding = getNumberField(&item->fields[i]); break;
			case STATSDEFERRED  : q->stats.deferred = getNumberField(&item->fields[i]); break;
			case STATSCOMPLETED : q->stats.completed = getNumberField(&item->fields[i]); break;
			case STATSEXITED    : q->stats.exited = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	return 0;
}

static int deserialize_jersResource(msg_item * item, jersResource *r) {
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case RESNAME   : r->name = getStringField(&item->fields[i]); break;
			case RESCOUNT  : r->count = getNumberField(&item->fields[i]); break;
			case RESINUSE  : r->inuse = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	return 0;
}

static int deserialize_jersAgent(msg_item * item, jersAgent *a) {
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case NODE       : a->host = getStringField(&item->fields[i]); break;
			case CONNECTED  : a->connected = getBoolField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	return 0;
}

static int deserialize_jersJob(msg_item * item, jersJob *j) {
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID     : j->jobid = getNumberField(&item->fields[i]); break;
			case JOBNAME   : j->jobname = getStringField(&item->fields[i]); break;
			case QUEUENAME : j->queue = getStringField(&item->fields[i]); break;
			case EXITCODE  : j->exit_code = getNumberField(&item->fields[i]); break;
			case SIGNAL    : j->signal = getNumberField(&item->fields[i]); break;
			case PRIORITY  : j->priority = getNumberField(&item->fields[i]); break;
			case STATE     : j->state = getNumberField(&item->fields[i]); break;
			case NICE      : j->nice = getNumberField(&item->fields[i]); break;
			case UID       : j->uid = getNumberField(&item->fields[i]); break;
			case SUBMITTER : j->submitter = getNumberField(&item->fields[i]); break;
			case SHELL     : j->shell = getStringField(&item->fields[i]); break;
			case PRECMD    : j->pre_command = getStringField(&item->fields[i]); break;
			case POSTCMD   : j->post_command = getStringField(&item->fields[i]); break;
			case STDOUT    : j->stdout = getStringField(&item->fields[i]); break;
			case STDERR    : j->stderr = getStringField(&item->fields[i]); break;
			case ARGS      : j->argc = getStringArrayField(&item->fields[i], &j->argv); break;
			case DEFERTIME : j->defer_time = getNumberField(&item->fields[i]); break;
			case SUBMITTIME: j->submit_time = getNumberField(&item->fields[i]); break;
			case STARTTIME : j->start_time = getNumberField(&item->fields[i]); break;
			case FINISHTIME: j->finish_time = getNumberField(&item->fields[i]); break;
			case TAGS      : j->tag_count = getStringMapField(&item->fields[i], (key_val_t **)&j->tags); break;
			case RESOURCES : j->res_count = getStringArrayField(&item->fields[i], &j->resources); break;
			case NODE      : j->node = getStringField(&item->fields[i]); break;
			case PENDREASON: j->pend_reason = getNumberField(&item->fields[i]); break;
			case FAILREASON: j->fail_reason = getNumberField(&item->fields[i]); break;
			case JOBPID    : j->pid = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	return 0;
}

JERS_EXPORT int jersGetJob(jobid_t jobid, const jersJobFilter * filter, jersJobInfo * job_info) {
	if (jersInitAPI(NULL))
		return 1;

	job_info->count = 0;
	job_info->jobs = NULL;

	buff_t b;

	initRequest(&b, CMD_GET_JOB, 1);

	if (jobid) {
		JSONAddInt(&b, JOBID, jobid);
	} else if (filter) {
		if (filter->filter_fields) {

			if (filter->filter_fields & JERS_FILTER_JOBNAME)
				JSONAddString(&b, JOBNAME, filter->filters.job_name);

			if (filter->filter_fields & JERS_FILTER_QUEUE)
				JSONAddString(&b, QUEUENAME, filter->filters.queue_name);

			if (filter->filter_fields & JERS_FILTER_STATE)
				JSONAddInt(&b, STATE, filter->filters.state);

			if (filter->filter_fields & JERS_FILTER_TAGS)
				JSONAddMap(&b, TAGS, filter->filters.tag_count, (key_val_t *)filter->filters.tags);

			if (filter->filter_fields & JERS_FILTER_RESOURCES)
				JSONAddStringArray(&b, RESOURCES, filter->filters.res_count, filter->filters.resources);

			if (filter->filter_fields & JERS_FILTER_UID)
				JSONAddInt(&b, UID, filter->filters.uid);

			if (filter->filter_fields & JERS_FILTER_SUBMITTER)
				JSONAddInt(&b, SUBMITTER, filter->filters.submitter);
		}

		if (filter->return_fields)
			JSONAddInt(&b, RETFIELDS, filter->return_fields);
	}

	if (sendRequest(&b))
		return 1;

	if (readResponse())
		return 1;

	if (msg.item_count) {
		job_info->jobs = calloc(sizeof(jersJob) *  msg.item_count, 1);

		for (int64_t i = 0; i < msg.item_count; i++) {
			deserialize_jersJob(&msg.items[i], &job_info->jobs[i]);
		}
	}

	job_info->count = msg.item_count;

	free_message(&msg);

	return 0;
}


JERS_EXPORT int jersDelJob(jobid_t jobid) {
	if (jersInitAPI(NULL))
		return 1;

	buff_t b;

	initRequest(&b, CMD_DEL_JOB, 1);

	JSONAddInt(&b, JOBID, jobid);

	if (sendRequest(&b))
		return 1;

	if (readResponse())
		return 1;

	free_message(&msg);

	return 0;
}

JERS_EXPORT void jersInitJobAdd(jersJobAdd * j) {
	memset(j, 0, sizeof(jersJobAdd));
	j->priority = -1;
	j->defer_time = -1;
}

JERS_EXPORT jobid_t jersAddJob(const jersJobAdd * j) {
	jobid_t new_jobid = 0;

	if (jersInitAPI(NULL))
		return 0;

	/* Sanity Checks */
	if (!j) {
		setJersErrno(JERS_ERR_INVARG, NULL);
		return 0;
	}

	if (j->argc <=0 || j->argv == NULL) {
		setJersErrno(JERS_ERR_INVARG, "argc/argv must be populated");
		return 0;
	}

	if (j->env_count > 0 && j->envs == NULL) {
		setJersErrno(JERS_ERR_INVARG, "env_count populated, but env not passed");
		return 0;
	}

	buff_t b;
	initRequest(&b, CMD_ADD_JOB, 1);

	JSONAddStringArray(&b, ARGS, j->argc, j->argv);

	if (j->name)
		JSONAddString(&b, JOBNAME, j->name);

	if (j->queue)
		JSONAddString(&b, QUEUENAME, j->queue);

	if (j->uid > 0)
		JSONAddInt(&b, UID, j->uid);

	if (j->shell)
		JSONAddString(&b, SHELL, j->shell);

	if (j->priority >= 0)
		JSONAddInt(&b, PRIORITY, j->priority);

	if (j->hold)
		JSONAddBool(&b, HOLD, 1);

	if (j->nice)
		JSONAddInt(&b, NICE, j->nice);

	if (j->tag_count)
		JSONAddMap(&b, TAGS, j->tag_count, (key_val_t *)j->tags);

	if (j->res_count)
		JSONAddStringArray(&b, RESOURCES, j->res_count, j->resources);

	if (j->env_count)
		JSONAddStringArray(&b, ENVS, j->env_count, j->envs);

	if (j->jobid)
		JSONAddInt(&b, JOBID, j->jobid);

	if (j->wrapper) {
		JSONAddString(&b, WRAPPER, j->wrapper);
	} else {
		if (j->pre_cmd)
			JSONAddString(&b, PRECMD, j->pre_cmd);

		if (j->post_cmd)
			JSONAddString(&b, POSTCMD, j->post_cmd);
	}

	if (j->stdout)
		JSONAddString(&b, STDOUT, j->stdout);

	if (j->stderr)
		JSONAddString(&b, STDERR, j->stderr);

	if (j->defer_time != -1)
		JSONAddInt(&b, DEFERTIME, j->defer_time);

	if (sendRequest(&b))
		return 0;

	if(readResponse())
		return 0;

	/* Should just have a single JOBID field returned */
	if (msg.item_count != 1 || msg.items[0].field_count != 1 || msg.items[0].fields[0].number != JOBID) {
		setJersErrno(JERS_ERR_INVRESP, NULL);
		return 0;
	}

	new_jobid = getNumberField(&msg.items[0].fields[0]);

	free_message(&msg);
	return new_jobid;
}

JERS_EXPORT void jersInitJobMod(jersJobMod *j) {
	memset(j, 0, sizeof(jersJobMod));
	j->nice = -1;
	j->hold = -1;
	j->priority = -1;
	j->defer_time = -1;
}

JERS_EXPORT int jersModJob(const jersJobMod *j) {
	if (jersInitAPI(NULL))
		return 1;

	if (j->jobid == 0 ) {
		setJersErrno(JERS_ERR_INVARG, "No jobid provided");
		return 1;
	}

	buff_t b;
	initRequest(&b, CMD_MOD_JOB, 1);

	JSONAddInt(&b, JOBID, j->jobid);

	if (j->name)
		JSONAddString(&b, JOBNAME, j->name);

	if (j->queue)
		JSONAddString(&b, QUEUENAME, j->queue);

	if (j->defer_time != -1)
		JSONAddInt(&b, DEFERTIME, j->defer_time);

	if (j->restart)
		JSONAddBool(&b, RESTART, 1);

	if (j->nice != -1)
		JSONAddInt(&b, NICE, j->nice);

	if (j->priority != -1)
		JSONAddInt(&b, PRIORITY, j->priority);

	if (j->hold != -1)
		JSONAddBool(&b, HOLD, j->hold);

	if (j->env_count)
		JSONAddStringArray(&b, ENVS, j->env_count, j->envs);

	if (j->tag_count)
		JSONAddMap(&b, TAGS, j->tag_count, (key_val_t *)j->tags);

	if (j->res_count)
		JSONAddStringArray(&b, RESOURCES, j->res_count, j->resources);

	if (j->clear_resources)
		JSONAddBool(&b, CLEARRES, 1);

	if (sendRequest(&b)) {
		return 1;
	}

	if(readResponse())
		return 1;

	free_message(&msg);
	return 0;
}

JERS_EXPORT int jersSignalJob(jobid_t id, int signum) {
	int status = 1;

	if (jersInitAPI(NULL))
		return 1;

	if (id == 0 ) {
		setJersErrno(JERS_ERR_INVARG, "No jobid provided");
		return 1;
	}

	if (signum < 0 || signum >= SIGRTMAX) {
		setJersErrno(JERS_ERR_INVARG, "Invalid signum provided");
		return 1;
	}

	/* Serialise the request */
	buff_t b;

	initRequest(&b, CMD_SIG_JOB, 1);

	JSONAddInt(&b, JOBID, id);
	JSONAddInt(&b, SIGNAL, signum);

	if (sendRequest(&b))
		return 1;

	if(readResponse())
		return 1;
printf("msg.command: %s\n", msg.command);
	if (msg.command && strcmp(msg.command, "0") == 0)
		status = 0;

	free_message(&msg);

	return status;
}


JERS_EXPORT void jersInitQueueMod(jersQueueMod *q) {
	q->name = NULL;
	q->desc = NULL;
	q->node = NULL;
	q->state = -1;
	q->job_limit = -1;
	q->priority = -1;
	q->default_queue = -1;
}

JERS_EXPORT void jersInitQueueAdd(jersQueueAdd *q) {
	q->name = NULL;
	q->desc = NULL;
	q->node = NULL;
	q->state = -1;
	q->job_limit = -1;
	q->priority = -1;
	q->default_queue = -1;
}

/* Note: filter is currently not used for queues */

JERS_EXPORT int jersGetQueue(const char * name, const jersQueueFilter * filter, jersQueueInfo * info) {
	if (jersInitAPI(NULL)) 
		return 0;

	(void) filter;

	info->count = 0;
	info->queues = NULL;

	buff_t b;

	initRequest(&b, CMD_GET_QUEUE, 1);

	if (name)
		JSONAddString(&b, QUEUENAME, name);
	else
		JSONAddString(&b, QUEUENAME, "*");

	if (sendRequest(&b))
		return 1;

	if(readResponse())
		return 1;

	int64_t i;

	info->queues = calloc(sizeof(jersQueue) *  msg.item_count, 1);

	for (i = 0; i < msg.item_count; i++) {
		deserialize_jersQueue(&msg.items[i], &info->queues[i]);
	}

	info->count = msg.item_count;

	free_message(&msg);

	return 0;
}

JERS_EXPORT void jersFreeQueueInfo(jersQueueInfo * info) {
	int i;

	for (i = 0; i < info->count; i++) {
		jersQueue * queue = &info->queues[i];

		free(queue->name);
		free(queue->desc);
		free(queue->node);
	}

	free(info->queues);
	return;
}

JERS_EXPORT int jersAddQueue(const jersQueueAdd *q) {

	if (jersInitAPI(NULL))
		return 1;

	if (q->name == NULL) {
		setJersErrno(JERS_ERR_INVARG, "No queue name provided");
		return 1;
	}

	if (q->node == NULL) {
		setJersErrno(JERS_ERR_INVARG, "No node provided");
		return 1;
	}

	buff_t b;

	initRequest(&b, CMD_ADD_QUEUE, 1);

	JSONAddString(&b, QUEUENAME, q->name);
	JSONAddString(&b, NODE, q->node);

	if (q->desc)
		JSONAddString(&b, DESC, q->desc);

	if (q->job_limit != -1)
		JSONAddInt(&b, JOBLIMIT, q->job_limit);

	if (q->priority != -1)
		JSONAddInt(&b, PRIORITY, q->priority);

	if (q->state != -1)
		JSONAddInt(&b, STATE, q->state);

	if (q->default_queue != -1)
		JSONAddBool(&b, DEFAULT, q->default_queue);

	if (sendRequest(&b))
		return 1;

	if(readResponse())
		return 1;

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersModQueue(const jersQueueMod *q) {
	if (jersInitAPI(NULL))
		return 1;

	if (q->name == NULL) {
		setJersErrno(JERS_ERR_INVARG, "No queue name provided");
		return 1;
	}

	if (q->desc == NULL && q->node == NULL && q->job_limit == -1 && q->priority == -1 && q->state == -1 && q->default_queue == -1) {
		setJersErrno(JERS_ERR_NOCHANGE, NULL);
		return 1;
	}

	buff_t b;

	initRequest(&b, CMD_MOD_QUEUE, 1);

	JSONAddString(&b, QUEUENAME, q->name);

	if (q->node)
		JSONAddString(&b, NODE, q->node);

	if (q->desc)
		JSONAddString(&b, DESC, q->desc);

	if (q->job_limit != -1)
		JSONAddInt(&b, JOBLIMIT, q->job_limit);

	if (q->priority != -1)
		JSONAddInt(&b, PRIORITY, q->priority);

	if (q->state != -1)
		JSONAddInt(&b, STATE, q->state);

	if (q->default_queue != -1)
		JSONAddBool(&b, DEFAULT, q->default_queue);

	if (sendRequest(&b))
		return 1;

	if(readResponse())
		return 1;

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersDelQueue(const char *name) {
	if (jersInitAPI(NULL))
		return 1;

	buff_t b;

	initRequest(&b, CMD_DEL_QUEUE, 1);

	JSONAddString(&b, QUEUENAME, name);

	if (sendRequest(&b))
		return 1;

	if (readResponse())
		return 1;

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersAddResource(const char *name, int count) {
	if (jersInitAPI(NULL))
		return 1;

	if (name == NULL) {
		setJersErrno(JERS_ERR_INVARG, NULL);
		return 1;
	}

	buff_t b;
	
	initRequest(&b, CMD_ADD_RESOURCE, 1);

	JSONAddString(&b, RESNAME, name);

	if (count)
		JSONAddInt(&b, RESCOUNT, count);

	if (sendRequest(&b))
		return 1;

	if (readResponse())
		return 1;

	free_message(&msg);

	return 0;
}
JERS_EXPORT int jersGetResource(const char * name, const jersResourceFilter *filter, jersResourceInfo *info) {
	if (jersInitAPI(NULL))
		return 1;

	(void) filter;

	info->count = 0;
	info->resources = NULL;

	buff_t b;

	initRequest(&b, CMD_GET_RESOURCE, 1);

	if (name)
		JSONAddString(&b, RESNAME, name);
	else
		JSONAddString(&b, RESNAME, "*");

	if (sendRequest(&b))
		return 1;

	if(readResponse())
		return 1;

	int64_t i;

	info->resources = calloc(sizeof(jersResource) *  msg.item_count, 1);

	for (i = 0; i < msg.item_count; i++) {
		deserialize_jersResource(&msg.items[i], &info->resources[i]);
	}

	info->count = msg.item_count;

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersModResource(const char *name, int new_count) {
	if (jersInitAPI(NULL))
		return 1;

	buff_t b;

	initRequest(&b, CMD_MOD_RESOURCE, 1);

	JSONAddString(&b, RESNAME, name);
	JSONAddInt(&b, RESCOUNT, new_count);

	if (sendRequest(&b))
		return 1;

	if (readResponse())
		return 1;

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersDelResource(const char *name) {
	if (jersInitAPI(NULL))
		return 1;

	buff_t b;

	initRequest(&b, CMD_DEL_RESOURCE, 1);

	JSONAddString(&b, RESNAME, name);

	if (sendRequest(&b))
		return 1;

	if (readResponse())
		return 1;

	free_message(&msg);

	return 0;
}

JERS_EXPORT void jersFreeResourceInfo(jersResourceInfo *info) {
	for (int i = 0; i < info->count; i++) {
		jersResource * res = &info->resources[i];

		free(res->name);
	}

	free(info->resources);
	return;
}

JERS_EXPORT int jersGetAgents(const char * name, jersAgentInfo *info) {
	if (jersInitAPI(NULL))
		return 1;

	info->count = 0;
	info->agents = NULL;

	buff_t b;

	initRequest(&b, CMD_GET_AGENT, 1);

	if (name)
		JSONAddString(&b, NODE, name);

	if (sendRequest(&b))
		return 1;

	if(readResponse())
		return 1;

	int64_t i;

	info->agents = calloc(sizeof(jersAgent) *  msg.item_count, 1);

	for (i = 0; i < msg.item_count; i++) {
		deserialize_jersAgent(&msg.items[i], &info->agents[i]);
	}

	info->count = msg.item_count;

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersGetStats(jersStats * s) {
	int i;

	if (jersInitAPI(NULL))
		return 1;

	memset(s, 0, sizeof(jersStats));

	buff_t b;

	initRequest(&b, CMD_STATS, 1);

	if (sendRequest(&b))
		return 1;

	if (readResponse())
		return 1;

	msg_item * item = &msg.items[0];

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case STATSRUNNING   : s->current.running = getNumberField(&item->fields[i]); break;
			case STATSPENDING   : s->current.pending = getNumberField(&item->fields[i]); break;
			case STATSDEFERRED  : s->current.deferred = getNumberField(&item->fields[i]); break;
			case STATSHOLDING   : s->current.holding = getNumberField(&item->fields[i]); break;
			case STATSCOMPLETED : s->current.completed = getNumberField(&item->fields[i]); break;
			case STATSEXITED    : s->current.exited = getNumberField(&item->fields[i]); break;
			case STATSUNKNOWN   : s->current.unknown = getNumberField(&item->fields[i]); break;


			case STATSTOTALSUBMITTED : s->total.submitted = getNumberField(&item->fields[i]); break;
			case STATSTOTALSTARTED   : s->total.started = getNumberField(&item->fields[i]); break;
			case STATSTOTALCOMPLETED : s->total.completed = getNumberField(&item->fields[i]); break;
			case STATSTOTALEXITED    : s->total.exited = getNumberField(&item->fields[i]); break;
			case STATSTOTALDELETED   : s->total.deleted = getNumberField(&item->fields[i]); break;
			case STATSTOTALUNKNOWN   : s->total.unknown = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersSetTag(jobid_t id, const char * key, const char * value) {
	if (jersInitAPI(NULL))
		return 1;

	buff_t b;

	initRequest(&b, CMD_SET_TAG, 1);

	JSONAddInt(&b, JOBID, id);
	JSONAddString(&b, TAG_KEY, key);

	if (value)
		JSONAddString(&b, TAG_VALUE, value);

	if (sendRequest(&b))
		return 1;

	if(readResponse())
		return 1;

	free_message(&msg);

	return 0;
}

JERS_EXPORT int jersDelTag(jobid_t id, const char * key) {
	if (jersInitAPI(NULL))
		return 1;

	buff_t b;

	initRequest(&b, CMD_DEL_TAG, 1);

	JSONAddInt(&b, JOBID, id);
	JSONAddString(&b, TAG_KEY, key);

	if (sendRequest(&b)) {
		return 1;
	}

	if (readResponse())
		return 1;

	free_message(&msg);

	return 0;
}
