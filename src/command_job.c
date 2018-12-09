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
#include <jers.h>
#include <commands.h>
#include <fields.h>

#include <time.h>
#include <pwd.h>
#include <fnmatch.h>

struct jobResource * convertResourceStrings(int res_count, char ** res_strings) {
	struct jobResource * resources = malloc(sizeof(struct jobResource) * res_count);
	int i;

	/* Check the resources */
	for (i = 0; i < res_count; i++) {
		struct resource * res = NULL;
		char * ptr = strchr(res_strings[i], ':');

		if (ptr) {
			*ptr = '\0';
			resources[i].needed = atoi(ptr + 1);
		} else {
			resources[i].needed = 1;
		}

		HASH_FIND_STR(server.resTable, res_strings[i], res);

		if (res == NULL) {
			free(resources);
			return NULL;
		}

		resources[i].res = res;
	}

	return resources;
}

char ** convertResourceToStrings(int res_count, struct jobResource * res) {
	char ** res_strings = NULL;
	int i;

	res_strings = malloc(sizeof(char *) * res_count);


	for (i = 0; i < res_count; i++) {
		res_strings[i] = malloc(strlen(res[i].res->name) + 16);
		sprintf(res_strings[i], "%s:%d", res[i].res->name, res[i].needed);
	}

	return res_strings; 
}

void * deserialize_add_job(msg_t * t) {
	jersJobAdd * s = calloc(sizeof(jersJobAdd), 1);
	int i;

	s->uid = -1; //TODO: == clients uid
	s->priority = JERS_JOB_DEFAULT_PRIORITY;

	for (i = 0; i < t->items[0].field_count; i++) {
		switch(t->items[0].fields[i].number) {
			case JOBNAME  : s->name = getStringField(&t->items[0].fields[i]); break;
			case QUEUENAME: s->queue = getStringField(&t->items[0].fields[i]); break;
			case UID      : s->uid = getNumberField(&t->items[0].fields[i]); break;
			case SHELL    : s->shell = getStringField(&t->items[0].fields[i]); break;
			case PRIORITY : s->priority = getNumberField(&t->items[0].fields[i]); break;
			case HOLD     : s->hold = getBoolField(&t->items[0].fields[i]); break;
			case ENVS     : s->env_count = getStringArrayField(&t->items[0].fields[i], &s->envs); break;
			case ARGS     : s->argc = getStringArrayField(&t->items[0].fields[i], &s->argv); break;
			case PRECMD   : s->pre_cmd = getStringField(&t->items[0].fields[i]); break;
			case POSTCMD  : s->post_cmd = getStringField(&t->items[0].fields[i]); break;
			case DEFERTIME: s->defer_time = getNumberField(&t->items[0].fields[i]); break;
			case TAGS     : s->tag_count = getStringArrayField(&t->items[0].fields[i], &s->tags); break;
			case RESOURCES: s->res_count = getStringArrayField(&t->items[0].fields[i], &s->resources); break;
			case STDOUT   : s->stdout = getStringField(&t->items[0].fields[i]); break;
			case STDERR   : s->stderr = getStringField(&t->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field %d encountered - Ignoring\n",t->items[0].fields[i].number); break;
		}

	}

	return s;
}

void * deserialize_get_job(msg_t * t) {
	jersJobFilter * s = calloc(sizeof(jersJobFilter), 1);
	msg_item * item = &t->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : s->jobid = getNumberField(&item->fields[i]); break;
			case JOBNAME  : s->filters.job_name = getStringField(&item->fields[i]); s->filter_fields |= JERS_FILTER_JOBNAME ; break;
			case QUEUENAME: s->filters.queue_name = getStringField(&item->fields[i]); s->filter_fields |= JERS_FILTER_QUEUE ; break;
			case STATE    : s->filters.state = getNumberField(&item->fields[i]); s->filter_fields |= JERS_FILTER_STATE ; break;
			case TAGS     : s->filters.tag_count = getStringArrayField(&item->fields[i], &s->filters.tags); s->filter_fields |= JERS_FILTER_TAGS ; break;
			case RESOURCES: s->filters.res_count = getStringArrayField(&item->fields[i], &s->filters.resources); s->filter_fields |= JERS_FILTER_RESOURCES ; break;
			case UID      : s->filters.uid = getNumberField(&item->fields[i]); s->filter_fields |= JERS_FILTER_UID ; break;

			case RETFIELDS: s->return_fields = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",t->items[0].fields[i].name); break;
		}

		/* If a jobid was provided, we ignore everything else */
		if (s->jobid)
			break;
	}

	return s;
}

void * deserialize_mod_job(msg_t * t) {
	jersJobMod * jm = calloc(sizeof(jersJobMod), 1);
	msg_item * item = &t->items[0];
	int i;

	jm->defer_time = -1;
	jm->nice = -1;
	jm->priority = -1;
	jm->hold = -1;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : jm->jobid = getNumberField(&item->fields[i]); break;
			case JOBNAME  : jm->job_name = getStringField(&item->fields[i]); break;
			case QUEUENAME: jm->queue = getStringField(&item->fields[i]); break;
			case DEFERTIME: jm->defer_time = getNumberField(&item->fields[i]); break;
			case NICE     : jm->nice = getNumberField(&item->fields[i]); break;
			case PRIORITY : jm->priority = getNumberField(&item->fields[i]); break;
			case HOLD     : jm->hold = getBoolField(&item->fields[i]); break;
			case RESTART  : jm->restart = getBoolField(&item->fields[i]); break;

			case ENVS     : jm->env_count = getStringArrayField(&item->fields[i], &jm->envs); break;
			case TAGS     : jm->tag_count = getStringArrayField(&item->fields[i], &jm->tags); break;
			case RESOURCES: jm->res_count = getStringArrayField(&item->fields[i], &jm->resources); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",t->items[0].fields[i].name); break;
		}
	}
	return jm;
}


void * deserialize_del_job(msg_t * t) {
	jersJobDel * jd = calloc(sizeof(jersJobDel), 1);


	return jd;
}

void serialize_jersJob(resp_t * r, struct job * j, int fields) {
	respAddMap(r);

	if (fields == 0 || fields & JERS_RET_JOBID)
		addIntField(r, JOBID, j->jobid);

	if (fields == 0 || fields & JERS_RET_JOBID)
		addStringField(r, JOBNAME, j->jobname);

	if (fields == 0 || fields & JERS_RET_QUEUE)
		addStringField(r, QUEUENAME, j->queue->name);

	if (fields == 0 || fields & JERS_RET_STATE)
		addIntField(r, STATE, j->state);

	//TODO: Add return flags for all fields
	if (fields == 0) {
		addIntField(r, PRIORITY, j->priority);
		addIntField(r, NICE, j->nice);
		addIntField(r, UID, j->uid);

		if (j->shell)
			addStringField(r, SHELL, j->shell);
		if (j->pre_cmd)
			addStringField(r, PRECMD, j->pre_cmd);
		if (j->post_cmd)
			addStringField(r, POSTCMD, j->post_cmd);
		if (j->stdout)
			addStringField(r, STDOUT, j->stdout);
		if (j->stderr)
			addStringField(r, STDERR, j->stderr);

		addStringArrayField(r, ARGS, j->argc, j->argv);

		if (j->defer_time)
			addIntField(r, DEFERTIME, j->defer_time);

		addIntField(r, SUBMITTIME, j->submit_time);

		if (j->start_time)
			addIntField(r, STARTTIME, j->start_time);
		if (j->finish_time)
			addIntField(r, FINISHTIME, j->finish_time);
		if (j->tag_count)
			addStringArrayField(r, TAGS, j->tag_count, j->tags);

		if (j->res_count) {
			int i;
			char ** res_strings = convertResourceToStrings(j->res_count, j->req_resources);
			addStringArrayField(r, RESOURCES, j->res_count, res_strings);

			for (i = 0; i < j->res_count; i++) {
				free(res_strings[i]);
			}

			free(res_strings[i]);
		}
	}

	respCloseMap(r);
}

int command_add_job(client * c, void * args) {
	jersJobAdd * s = args;
	struct job * j = NULL;
	struct queue * q = NULL;
	int state = 0;
	struct jobResource * resources = NULL;

	/* Validate the request first up */
	if (s->queue == NULL) {
		q = server.defaultQueue;
	} else {
		HASH_FIND_STR(server.queueTable, s->queue, q);

		if (q == NULL) {
			appendError(c, "-NOQUEUE Queue not found\n");
			return -1;
		}

		free(s->queue);
	}

	if (s->uid == 0) {
		appendError(c, "-BADUSER Jobs not allowed to run as root\n");
		return -1;
	}

	if (getpwuid(s->uid) == NULL) {
		appendError(c, "-NOUSER User not found\n");
		return -1;
	}

	if (s->res_count) {
		resources = convertResourceStrings(s->res_count, s->resources);

		if (resources == NULL) {
			appendError(c, "-INVRES A requested resource does not exist\n");
			return -1;
		}
	}

	/* Request looks good. Allocate a new job structure and jobid */
	j = calloc(sizeof(struct job), 1);
	j->jobid = getNextJobID();

	/* Fill out the job structure */
	j->jobname = s->name;
	j->queue = q;
	j->shell = s->shell;
	j->stdout = s->stdout;
	j->stderr = s->stderr;
	j->pre_cmd = s->pre_cmd;
	j->post_cmd = s->post_cmd;
	j->argc = s->argc;
	j->argv = s->argv;
	j->env_count = s->env_count;
	j->envs = s->envs;
	j->uid = s->uid;
	j->defer_time = s->defer_time;
	j->priority = s->priority;
	j->tag_count = s->tag_count;
	j->tags = s->tags;

	if (resources) {
		j->res_count = s->res_count;
		j->req_resources = resources;
	}

	if (j->defer_time)
		state = JERS_JOB_DEFERRED;
	else if (s->hold)
		state = JERS_JOB_HOLDING;
	else
		state = JERS_JOB_PENDING;

	j->submit_time = time(NULL);

	/* Add it to the hashtable */
	addJob(j, state, 1);

	/* Return the jobid */
	resp_t * response = respNew();

	respAddArray(response);
	respAddSimpleString(response, "RESP");
	respAddInt(response, 1);
	respAddMap(response);
	addIntField(response, JOBID, j->jobid);
	respCloseMap(response);
	respCloseArray(response);

	size_t buff_length = 0;
	char * buff = respFinish(response, &buff_length);
	appendResponse(c, buff, buff_length);
	free(buff);

	print_msg(JERS_LOG_DEBUG, "SUBMIT - JOBID %d created", j->jobid);

	return 0;
}

int matches(const char * pattern, const char * string) {
	if (strchr(pattern, '*') || strchr(pattern, '?')) {
		if (fnmatch(pattern, string, 0) == 0)
			return 0;
		else 
			return 1;
	} else {
		return strcmp(string, pattern);
	}
}

int command_get_job(client *c, void * args) {
	jersJobFilter * s = args;
	struct queue * q = NULL;
	struct job * j = NULL;

	int64_t count = 0;

	resp_t * r = NULL;

	/* JobId? Just look it up and return the result */
	if (s->jobid) {
		HASH_FIND_INT(server.jobTable, &s->jobid, j);

		if (j == NULL || (j->internal_state &JERS_JOB_FLAG_DELETED)) {
			appendError(c, "-NOJOB Job not found\n");
			return 1;
		}

		r = respNew();
		respAddArray(r);
		respAddSimpleString(r, "RESP");
		respAddInt(r, 1);
		serialize_jersJob(r, j, 0);
		count = 1;

	} else {
		/* If a queue filter has been provided, look it up first */
		if (s->filter_fields & JERS_FILTER_QUEUE) {

			HASH_FIND_STR(server.queueTable, s->filters.queue_name, q);

			if (q == NULL) {
				appendError(c, "-BADQUEUE Queue not found\n");
				return -1;
			}
		}

		r = respNew();
		respAddArray(r);
		respAddSimpleString(r, "RESP");
		respAddInt(r, 1);

		respAddArray(r);

		/* Loop through all non-deleted jobs and match against the criteria provided
		 * Add the jobs to the response as we go */

		for (j = server.jobTable; j != NULL; j = j->hh.next) {

			if (j->internal_state &JERS_JOB_FLAG_DELETED)
				continue;

			/* Try and filter on the easier criteria first */

			if (s->filter_fields & JERS_FILTER_STATE) {
				if (!(s->filters.state &j->state))
					continue;
			}

			if (s->filter_fields & JERS_FILTER_QUEUE) {
				if (j->queue != q)
					continue;
			}

			if (s->filter_fields & JERS_FILTER_UID) {
				if (s->filters.uid != j->uid)
					continue;
			}

			if (s->filter_fields & JERS_FILTER_JOBNAME) {
				if (strcmp(j->jobname, s->filters.job_name) != 0)
					continue;
			}

			/* Check that all the tag filters provided match the job */
			if (s->filter_fields & JERS_FILTER_TAGS) {
				int i;
				int match = 1;
				for (i = 0; i < s->filters.tag_count; i++) {
					int k;
					for (k = 0; k < j->tag_count; k++) {
						if (matches(s->filters.tags[i], j->tags[k]) == 0)
							break;
					}

					if (k == j->tag_count) {
						match = 0;
						break;
					}
				}

				if (!match)
					continue;
			}

			/* Made it here, add it to our response */
			serialize_jersJob(r, j, s->return_fields);
			count++;
		}

		respCloseArray(r);
	}

	respCloseArray(r);
	size_t reply_length = 0;
	char * reply = respFinish(r, &reply_length);
	appendResponse(c, reply, reply_length);
	free(reply);
	return 0;
}

int command_mod_job(client *c, void *args) {
	jersJobMod *mj = args;
	struct job * j = NULL;
	struct queue * q = NULL;
	int state = 0;
	int dirty = 0;
	int hold = 0;

	if (mj->jobid == 0) {
		appendError(c, "-NOJOB Job does not exist\n");
		return 0;
	}

	HASH_FIND_INT(server.jobTable, &mj->jobid, j);

	state = j->state;

	if (j == NULL || j->internal_state &JERS_JOB_FLAG_DELETED) {
		appendError(c, "-NOJOB Job does not exist\n");
		return 0;
	}

	if ((j->state == JERS_JOB_COMPLETED || j->state == JERS_JOB_EXITED) && mj->restart != 1) {
		appendError(c, "-INVARG Unable to modify a completed job without restart flag\n");
		return 0;
	}

	if (j->state == JERS_JOB_RUNNING || j->internal_state &JERS_JOB_FLAG_STARTED) {
		appendError(c, "-INVARG Unable to modify a running job\n");
		return 0;
	}

	if (mj->priority != -1) {
		if (mj->priority < JERS_JOB_MIN_PRIORITY || mj->priority > JERS_JOB_MAX_PRIORITY) {
			appendError(c, "-INVARG Invalid priority specified\n");
			return 0;
		}
	}

	if (mj->queue) {
		HASH_FIND_STR(server.queueTable, mj->queue, q);

		if (q == NULL) {
			appendError(c, "-NOQUEUE Invalid queue specified\n");
			return 0;
		}

		free(mj->queue);
	}

	if (mj->job_name) {
		free(j->jobname);
		j->jobname = mj->job_name;
		dirty = 1;
	}

	if (q) {
		j->queue = q;
		dirty = 1;
	}

	if (mj->priority != -1) {
		j->priority = mj->priority;

		server.candidate_recalc = 1;
		dirty = 1;
	}

	if (mj->defer_time != -1) {
		j->defer_time = mj->defer_time;

		server.candidate_recalc = 1;
		dirty = 1;
	}

	if (mj->hold != -1) {
		hold = mj->hold == 0 ? 0 : 1;

		server.candidate_recalc = 1;
		dirty = 1;
	}

	if (mj->nice != -1) {
		j->nice = mj->nice;

		dirty = 1;
	}

	if (mj->env_count) {
		if (j->env_count)
			freeStringArray(j->env_count, &j->envs);

		j->env_count = mj->env_count;
		j->envs = mj->envs;;
	}

	if (mj->tag_count) {
		if (j->tag_count)
			freeStringArray(j->tag_count, &j->tags);

		j->tag_count = mj->tag_count;
		j->tags = mj->tags;
	}

	if (mj->res_count) {
		struct jobResource * resources = NULL;

		if (j->res_count)
			free(j->req_resources);

		resources = convertResourceStrings(mj->res_count, mj->resources);

		freeStringArray(mj->res_count, &mj->resources);

		if (resources == NULL) {
			appendError(c, "-INVRES A requested resource does not exist\n");
			return -1;
		}

		j->req_resources = resources;
		j->res_count = mj->res_count;

		server.candidate_recalc = 1;
		dirty = 1;
	}

	/* Need to clear some fields if this job has previously been completed */
	if (j->state == JERS_JOB_COMPLETED || j->state == JERS_JOB_EXITED) {
		j->exitcode = 0;
		j->signal = 0;
		j->start_time = 0;
		j->finish_time = 0;

		j->dirty = 1;
		server.candidate_recalc = 1;
	}

	if (j->defer_time)
		state = JERS_JOB_DEFERRED;
	else if (hold)
		state = JERS_JOB_HOLDING;
	else
		state = JERS_JOB_PENDING;

	changeJobState(j, state, dirty);

	resp_t * r = respNew();
	respAddSimpleString(r, "0");

	size_t reply_length = 0;
	char * reply = respFinish(r, &reply_length);
	appendResponse(c, reply, reply_length);
	free(reply);
	return 0;

	return 0;
}

int command_del_job(client * c, void * args) {
	jersJobDel * jd = args;
	struct job * j = NULL;
	resp_t * r = NULL;

	HASH_FIND_INT(server.jobTable, &jd->jobid, j);

	if (!j || j->internal_state &JERS_JOB_FLAG_DELETED) {
		appendError(c, "-NOJOB Job does not exist\n");
		return 0;
	}

	j->internal_state |= JERS_JOB_FLAG_DELETED;
	changeJobState(j, 0, 1);

	r = respNew();
	respAddSimpleString(r, "0");

	size_t reply_length = 0;
	char * reply = respFinish(r, &reply_length);
	appendResponse(c, reply, reply_length);
	free(reply);
	return 0;
}
