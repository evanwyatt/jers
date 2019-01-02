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

#include <jers.h>
#include <resp.h>
#include <fields.h>
#include <buffer.h>

#define DEFAULT_CLIENT_TIMEOUT 60 // seconds

char * socket_path = NULL;
int fd = -1;
msg_t msg;
buff_t response = {0};
int initalised = 0;

int deserialize_jerJob(msg_item * item, struct jersJob * job);

static int jersConnect(void);

const char * jers_error_str[] = {
	"Ok",
	"Unknown Error",
	"Invalid argument",
	NULL

};

static int apierror = 0;	      /* Last error code set by API function. Reset before every call */
static const char * apierrorstring = NULL;  /* Custom error string populated by API functions */

static void setError(int error, const char * errorString) {
	apierror = error;
	apierrorstring = errorString ? errorString : NULL;
}

static int customInit(void * arg) {
	return 0;
}

static int defaultInit(void) {
	socket_path = "/var/run/jers/jers.socket";
	return 0;
}

int jersInitAPI(void * arg) {
	int rc = 0;

	/* We can be reinitalised by providing a config argument */
	if (initalised && !arg) {
		setError(JERS_OK, NULL);
		return 0;
	}

	if (arg) {
		rc = customInit(arg);
	} else {
		rc = defaultInit();
	}

	if (rc)
		return rc;

	if (jersConnect())
		return 1;

	buffNew(&response, 0);

	initalised = 1;

	return rc;
}

void jersFinish(void) {
	close(fd);
	buffFree(&response);
	initalised = 0;
	fd = -1;
}

/* Establish a connection to the main daemon */
static int jersConnect(void) {
	struct sockaddr_un addr;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;

	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
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
static int sendRequest(char * request, size_t length) {
	size_t total_sent = 0;

	while (total_sent < length) {
		ssize_t sent = send(fd, request + total_sent, length - total_sent, MSG_NOSIGNAL);

		if (sent == -1 && errno != EINTR) {
			fprintf(stderr, "send to jers daemon failed: %s\n", strerror(errno));
			return 1;
		}

		total_sent += sent;
	}

	return 0;
}

/* Block until we read a full response */
static int readResponse(void) {
	while (1) {
		/* Allocate more memory if we might need it */
		if (buffResize(&response, 0) != 0) {
			fprintf(stderr, "failed to resize response buffer\n");
			return 1;
		}

		ssize_t bytes_read = recv(fd, response.data + response.used, response.size - response.used, 0);

		if (bytes_read == -1) {
			if (errno == EINTR)
				continue;

			fprintf(stderr, "read from jers daemon failed: %s\n", strerror(errno));
			return 1;
		} else if (bytes_read == 0) {
			fprintf(stderr, "disconnected from jer daemon\n");
			return 1;
		}

		response.used += bytes_read;

		/* Got a full response yet? */
		int rc = load_message(&msg, &response);

		if (rc == 1)
			continue;

		if (rc < 0) {
			fprintf(stderr, "Failed to parse response from jers daemon\n");
			return 1;
		}

		break;
	}

	return 0;
}

int jersGetErrno(void) {
	return apierror;
}

const char * jersGetErrStr(int apierror) {
	return apierrorstring ? apierrorstring : jers_error_str[apierror];
}

void jersFreeJobInfo (jersJobInfo * info) {
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

		for (j = 0; j < job->argc; j++)
			free(job->argv[j]);

		free(job->argv);

		for (j = 0; j < job->tag_count; j++)
			free(job->tags[j]);

		free(job->tags);

		for (j = 0; j < job->res_count; j++)
			free(job->resources[j]);

		free(job->resources);
	}

	free(info->jobs);
}

int deserialize_jersQueue(msg_item * item, jersQueue *q) {
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case QUEUENAME : q->name = getStringField(&item->fields[i]); break;
			case DESC      : q->desc = getStringField(&item->fields[i]); break;
			case NODE      : q->node = getStringField(&item->fields[i]); break;
			case JOBLIMIT  : q->job_limit = getNumberField(&item->fields[i]); break;
			case STATE     : q->state = getNumberField(&item->fields[i]); break;
			case PRIORITY  : q->priority = getNumberField(&item->fields[i]); break;

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

int deserialize_jersResource(msg_item * item, jersResource *r) {
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

int deserialize_jersJob(msg_item * item, jersJob *j) {
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
			case TAGS      : j->tag_count = getStringArrayField(&item->fields[i], &j->tags); break;
			case RESOURCES : j->res_count = getStringArrayField(&item->fields[i], &j->resources); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	return 0;
}

int jersGetJob(jobid_t jobid, jersJobFilter * filter, jersJobInfo * job_info) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 0;
	}

	job_info->count = 0;
	job_info->jobs = NULL;

	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "GET_JOB");
	respAddInt(r, 1); // Version
	respAddMap(r);

	if (jobid) {
		addIntField(r, JOBID, jobid);
	} else if (filter) {
		if (filter->filter_fields) {

			if (filter->filter_fields & JERS_FILTER_JOBNAME)
				addStringField(r, JOBNAME, filter->filters.job_name);

			if (filter->filter_fields & JERS_FILTER_QUEUE)
				addStringField(r, QUEUENAME, filter->filters.queue_name);

			if (filter->filter_fields & JERS_FILTER_STATE)
				addIntField(r, STATE, filter->filters.state);

			if (filter->filter_fields & JERS_FILTER_TAGS)
				addStringArrayField(r, TAGS, filter->filters.tag_count, filter->filters.tags);

			if (filter->filter_fields & JERS_FILTER_RESOURCES)
				addStringArrayField(r, RESOURCES, filter->filters.res_count, filter->filters.resources);

			if (filter->filter_fields & JERS_FILTER_UID)
				addIntField(r, UID, filter->filters.uid);
		}

		if (filter->return_fields)
			addIntField(r, RETFIELDS, filter->return_fields);
	}

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if(readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, msg.error);
		free_message(&msg, NULL);
		return 1;
	}

	int64_t i;

	job_info->jobs = calloc(sizeof(jersJob) *  msg.item_count, 1);

	for (i = 0; i < msg.item_count; i++) {
		deserialize_jersJob(&msg.items[i], &job_info->jobs[i]);
	}

	job_info->count = msg.item_count;

	free_message(&msg, &response);

	return 0;
}


int jersDelJob(jobid_t jobid) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 0;
	}

	/* Serialise the request */
	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "DEL_JOB");
	respAddInt(r, 1);
	respAddMap(r);

	addIntField(r, JOBID, jobid);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 0;
	}

	free(request);

	if(readResponse())
		return 0;

	if (msg.error) {
		setError(JERS_INVALID, msg.error);
		free_message(&msg, NULL);
		return 0;
	}

	return 0;
}

void jersInitJobAdd(jersJobAdd * j) {
	memset(j, 0, sizeof(jersJobAdd));
	j->uid = -1;
	j->priority = -1;
	j->defer_time = -1;
}

jobid_t jersAddJob(jersJobAdd * j) {
	jobid_t new_jobid = 0;

	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 0;
	}

	/* Sanity Checks */
	if (!j) {
		setError(JERS_INVALID, NULL);
		return 0;
	}

	if (!j->name) {
		setError(JERS_INVALID, "Job name must be provided");
		return 0;
	}

	if (j->argc <=0 || j->argv == NULL) {
		setError(JERS_INVALID, "argc/argv must be populated");
		return 0;
	}

	if (j->env_count > 0 && j->envs == NULL) {
		setError(JERS_INVALID, "env_count populated, but env not passed");
		return 0;
	}

	/* Serialise the request */
	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "ADD_JOB");
	respAddInt(r, 1);
	respAddMap(r);

	addStringField(r, JOBNAME, j->name);
	addStringArrayField(r, ARGS, j->argc, j->argv);

	if (j->queue)
		addStringField(r, QUEUENAME, j->queue);

	if (j->uid >= 0)
		addIntField(r, UID, j->uid);

	if (j->shell)
		addStringField(r, SHELL, j->shell);

	if (j->priority >= 0)
		addIntField(r, PRIORITY, j->priority);

	if (j->hold)
		addBoolField(r, HOLD, 1);

	if (j->tag_count) {
		addStringArrayField(r, TAGS, j->tag_count, j->tags);
	}

	if (j->res_count) {
		addStringArrayField(r, RESOURCES, j->res_count, j->resources);
	}

	if (j->env_count) {
		addStringArrayField(r, ENVS, j->env_count, j->envs);
	}

	if (j->wrapper) {
		addStringField(r, WRAPPER, j->wrapper);
	} else {
		if (j->pre_cmd) {
			addStringField(r, PRECMD, j->pre_cmd);
		}

		if (j->post_cmd) {
			addStringField(r, POSTCMD, j->post_cmd);
		}
	}

	if (j->stdout)
		addStringField(r, STDOUT, j->stdout);

	if (j->stderr)
		addStringField(r, STDERR, j->stderr);

	if (j->defer_time != -1)
		addIntField(r, DEFERTIME, j->defer_time);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 0;
	}

	free(request);

	if(readResponse())
		return 0;

	if (msg.error) {
		setError(JERS_INVALID, msg.error);
		free_message(&msg, NULL);
		return 0;
	}

	/* Should just have a single JOBID field returned */
	if (msg.item_count != 1 || msg.items[0].field_count != 1 || msg.items[0].fields[0].type != RESP_TYPE_INT) {
		setError(JERS_INVALID, "Got invalid response from jers daemon");
		return 0;
	}

	new_jobid = getNumberField(&msg.items[0].fields[0]);

	free_message(&msg, &response);
	return new_jobid;
}

void jersInitJobMod(jersJobMod *j) {
	memset(j, 0, sizeof(jersJobMod));
	j->nice = -1;
	j->hold = -1;
	j->priority = -1;
	j->defer_time = -1;
}

int jersModJob(jersJobMod *j) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 1;
	}

	if (j->jobid == 0 ) {
		setError(JERS_INVALID, "No jobid provided");
		return 1;
	}

	/* Serialise the request */
	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "MOD_JOB");
	respAddInt(r, 1);
	respAddMap(r);

	addIntField(r, JOBID, j->jobid);

	if (j->name)
		addStringField(r, JOBNAME, j->name);

	if (j->queue)
		addStringField(r, QUEUENAME, j->queue);

	if (j->defer_time != -1)
		addIntField(r, DEFERTIME, j->defer_time);

	if (j->restart)
		addBoolField(r, RESTART, 1);

	if (j->nice != -1)
		addIntField(r, NICE, j->nice);

	if (j->priority != -1)
		addIntField(r, PRIORITY, j->priority);

	if (j->hold != -1)
		addBoolField(r, HOLD, j->hold);

	if (j->env_count)
		addStringArrayField(r, ENVS, j->env_count, j->envs);

	if (j->tag_count)
		addStringArrayField(r, TAGS, j->tag_count, j->tags);

	if (j->res_count)
		addStringArrayField(r, RESOURCES, j->res_count, j->resources);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if(readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, "Error modifying job");
		free_message(&msg, NULL);
		return 1;
	}

	free_message(&msg, &response);
	return 0;
}

int jersSignalJob(jobid_t id, int signum) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 1;
	}

	if (id == 0 ) {
		setError(JERS_INVALID, "No jobid provided");
		return 1;
	}

	if (signum <= 0 || signum >= NSIG) {
		setError(JERS_INVALID, "Invalid signum provided");
		return 1;
	}

	/* Serialise the request */
	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "SIG_JOB");
	respAddInt(r, 1);
	respAddMap(r);

	addIntField(r, JOBID, id);
	addIntField(r, SIGNAL, signum);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if(readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, "Error signaling job");
		free_message(&msg, NULL);
		return 1;
	}

	free_message(&msg, &response);
	return 0;
}


void jersInitQueueMod(jersQueueMod *q) {
	q->name = NULL;
	q->desc = NULL;
	q->node = NULL;
	q->state = -1;
	q->job_limit = -1;
	q->priority = -1;
}

void jersInitQueueAdd(jersQueueAdd *q) {
	q->name = NULL;
	q->desc = NULL;
	q->node = NULL;
	q->state = -1;
	q->job_limit = -1;
	q->priority = -1;
}

/* Note: filter is currently not used for queues */

int jersGetQueue(char * name, jersQueueFilter * filter, jersQueueInfo * info) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 0;
	}

	info->count = 0;
	info->queues = NULL;

	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "GET_QUEUE");
	respAddInt(r, 1); // Version
	respAddMap(r);

	if (name)
		addStringField(r, QUEUENAME, name);
	else
		addStringField(r, QUEUENAME, "*");

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if(readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, msg.error);
		free_message(&msg, NULL);
		return 1;
	}

	int64_t i;

	info->queues = calloc(sizeof(jersQueue) *  msg.item_count, 1);

	for (i = 0; i < msg.item_count; i++) {
		deserialize_jersQueue(&msg.items[i], &info->queues[i]);
	}

	info->count = msg.item_count;

	free_message(&msg, &response);

	return 0;
}

void jersFreeQueueInfo(jersQueueInfo * info) {
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

int jersAddQueue(jersQueueAdd *q) {

	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 1;
	}

	if (q->name == NULL) {
		setError(JERS_INVALID, "No queue name provided");
		return 1;
	}

	if (q->node == NULL) {
		setError(JERS_INVALID, "No node provided");
		return 1;
	}


	/* Serialise the request */
	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "ADD_QUEUE");
	respAddInt(r, 1);
	respAddMap(r);

	addStringField(r, QUEUENAME, q->name);
	addStringField(r, NODE, q->node);

	if (q->desc)
		addStringField(r, DESC, q->desc);

	if (q->job_limit != -1)
		addIntField(r, JOBLIMIT, q->job_limit);

	if (q->priority != -1)
		addIntField(r, PRIORITY, q->priority);

	if (q->state != -1)
		addIntField(r, STATE, q->state);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if(readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, "Error adding queue");
		free_message(&msg, NULL);
		return 1;
	}

	free_message(&msg, &response);

	return 0;
}

int jersModQueue(jersQueueMod *q) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 1;
	}

	if (q->name == NULL) {
		setError(JERS_INVALID, "No queue name provided");
		return 1;
	}

	if (q->desc == NULL && q->node == NULL && q->job_limit == -1 && q->priority == -1 && q->state == -1) {
		setError(JERS_INVALID, "Nothing to update");
		return 1;
	}


	/* Serialise the request */
	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "MOD_QUEUE");
	respAddInt(r, 1);
	respAddMap(r);

	addStringField(r, QUEUENAME, q->name);

	if (q->node)
		addStringField(r, NODE, q->node);

	if (q->desc)
		addStringField(r, DESC, q->desc);

	if (q->job_limit != -1)
		addIntField(r, JOBLIMIT, q->job_limit);

	if (q->priority != -1)
		addIntField(r, PRIORITY, q->priority);

	if (q->state != -1)
		addIntField(r, STATE, q->state);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if(readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, "Error modifying queue");
		free_message(&msg, NULL);
		return 1;
	}

	free_message(&msg, &response);

	return 0;
}

int jersAddResource(char *name, int count) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 1;
	}

	if (name == NULL)
		return 1;

	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "ADD_RESOURCE");
	respAddInt(r, 1);
	respAddMap(r);

	addStringField(r, RESNAME, name);

	if (count)
		addIntField(r, RESCOUNT, count);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if (readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, "Error adding resource");
		free_message(&msg, NULL);
		return 1;
	}

	free_message(&msg, &response);

	return 0;
}
int jersGetResource(char * name, jersResourceFilter *filter, jersResourceInfo *info) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 0;
	}

	info->count = 0;
	info->resources = NULL;

	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "GET_RESOURCE");
	respAddInt(r, 1); // Version
	respAddMap(r);

	if (name)
		addStringField(r, RESNAME, name);
	else
		addStringField(r, RESNAME, "*");

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if(readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, msg.error);
		free_message(&msg, NULL);
		return 1;
	}

	int64_t i;

	info->resources = calloc(sizeof(jersResource) *  msg.item_count, 1);

	for (i = 0; i < msg.item_count; i++) {
		deserialize_jersResource(&msg.items[i], &info->resources[i]);
	}

	info->count = msg.item_count;

	free_message(&msg, &response);

	return 0;
}

int jersModResource(char *name, int new_count) {
	return 0;
}

int jersDelResource(char *name) {
	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 0;
	}

	/* Serialise the request */
	resp_t * r = respNew();

	respAddArray(r);
	respAddSimpleString(r, "DEL_RESOURCE");
	respAddInt(r, 1);
	respAddMap(r);

	addStringField(r, RESNAME, name);

	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 0;
	}

	free(request);

	if(readResponse())
		return 0;

	if (msg.error) {
		setError(JERS_INVALID, msg.error);
		free_message(&msg, NULL);
		return 1;
	}

	return 0;
}

void jersFreeResourceInfo(jersResourceInfo *info) {
	return;
}

int jersGetStats(jersStats * s) {
	int i;

	if (jersInitAPI(NULL)) {
		setError(JERS_ERROR, NULL);
		return 1;
	}

	memset(s, 0, sizeof(jersStats));

	resp_t * r = respNew();
	respAddArray(r);
	respAddSimpleString(r, "STATS");
	respAddInt(r, 1);
	respAddMap(r);
	addIntField(r, JOBID, 0); // Dummy field
	respCloseMap(r);
	respCloseArray(r);

	size_t req_len;
	char * request = respFinish(r, &req_len);

	if (sendRequest(request, req_len)) {
		free(request);
		return 1;
	}

	free(request);

	if (readResponse())
		return 1;

	if (msg.error) {
		setError(JERS_INVALID, "Error getting stats");
		free_message(&msg, NULL);
		return 1;
	}

	msg_item * item = &msg.items[0];

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case STATSRUNNING   : s->current.running = getNumberField(&item->fields[i]); break;
			case STATSPENDING   : s->current.pending = getNumberField(&item->fields[i]); break;
			case STATSDEFERRED  : s->current.deferred = getNumberField(&item->fields[i]); break;
			case STATSHOLDING   : s->current.holding = getNumberField(&item->fields[i]); break;
			case STATSCOMPLETED : s->current.completed = getNumberField(&item->fields[i]); break;
			case STATSEXITED    : s->current.exited = getNumberField(&item->fields[i]); break;

			case STATSTOTALSUBMITTED : s->total.submitted = getNumberField(&item->fields[i]); break;
			case STATSTOTALSTARTED   : s->total.started = getNumberField(&item->fields[i]); break;
			case STATSTOTALCOMPLETED : s->total.completed = getNumberField(&item->fields[i]); break;
			case STATSTOTALEXITED    : s->total.exited = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	free_message(&msg, &response);

	return 0;
}
