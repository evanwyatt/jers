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
#include <error.h>

#include <time.h>
#include <pwd.h>

/* Convert a resource string and populate the passed in res structure */
int resourceStringToResource(const char * string, struct jobResource * res) {
	char * string_temp = strdup(string);
	char * ptr = strchr(string_temp, ':');
	struct resource * search_res = NULL;

	if (ptr) {
		*ptr = '\0';
		res->needed = atoi(ptr + 1);
	} else {
		res->needed = 1;
	}

	search_res = findResource(string_temp);

	if (search_res == NULL) {
		free(string_temp);
		return 1;
	}

	res->res = search_res;
	free(string_temp);
	return 0;
}

struct jobResource * convertResourceStrings(int res_count, char ** res_strings) {
	struct jobResource * resources = malloc(sizeof(struct jobResource) * res_count);

	/* Check the resources */
	for (int i = 0; i < res_count; i++) {
		if (resourceStringToResource(res_strings[i], &resources[i]) != 0) {
			free(resources);
			return NULL;
		}
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

	s->priority = JERS_JOB_DEFAULT_PRIORITY;

	for (i = 0; i < t->items[0].field_count; i++) {
		switch(t->items[0].fields[i].number) {
			case JOBID    : s->jobid = getNumberField(&t->items[0].fields[i]); break;
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
			case TAGS     : s->tag_count = getStringMapField(&t->items[0].fields[i], (key_val_t **)&s->tags); break;
			case RESOURCES: s->res_count = getStringArrayField(&t->items[0].fields[i], &s->resources); break;
			case STDOUT   : s->stdout = getStringField(&t->items[0].fields[i]); break;
			case STDERR   : s->stderr = getStringField(&t->items[0].fields[i]); break;
			case WRAPPER  : s->wrapper = getStringField(&t->items[0].fields[i]); break;
			case NICE     : s->nice =  getNumberField(&t->items[0].fields[i]); break;

			default: fprintf(stderr, "Unknown field %s encountered - Ignoring\n",t->items[0].fields[i].name); break;
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
			case TAGS     : s->filters.tag_count = getStringMapField(&item->fields[i], (key_val_t **)&s->filters.tags); s->filter_fields |= JERS_FILTER_TAGS ; break;
			case RESOURCES: s->filters.res_count = getStringArrayField(&item->fields[i], &s->filters.resources); s->filter_fields |= JERS_FILTER_RESOURCES ; break;
			case UID      : s->filters.uid = getNumberField(&item->fields[i]); s->filter_fields |= JERS_FILTER_UID ; break;
			case NODE     : s->filters.node = getStringField(&item->fields[i]); s->filter_fields |= JERS_FILTER_NODE ; break;

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
			case JOBNAME  : jm->name = getStringField(&item->fields[i]); break;
			case QUEUENAME: jm->queue = getStringField(&item->fields[i]); break;
			case DEFERTIME: jm->defer_time = getNumberField(&item->fields[i]); break;
			case NICE     : jm->nice = getNumberField(&item->fields[i]); break;
			case PRIORITY : jm->priority = getNumberField(&item->fields[i]); break;
			case HOLD     : jm->hold = getBoolField(&item->fields[i]); break;
			case RESTART  : jm->restart = getBoolField(&item->fields[i]); break;

			case ENVS     : jm->env_count = getStringArrayField(&item->fields[i], &jm->envs); break;
			case TAGS     : jm->tag_count = getStringMapField(&item->fields[i], (key_val_t **)&jm->tags); break;
			case RESOURCES: jm->res_count = getStringArrayField(&item->fields[i], &jm->resources); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",t->items[0].fields[i].name); break;
		}
	}
	return jm;
}


void * deserialize_del_job(msg_t * t) {
	jersJobDel * jd = calloc(sizeof(jersJobDel), 1);
	msg_item * item = &t->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : jd->jobid = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",t->items[0].fields[i].name); break;
		}
	}

	return jd;
}

void * deserialize_sig_job(msg_t * t) {
	jersJobSig * js = calloc(sizeof(jersJobSig), 1);
	msg_item * item = &t->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : js->jobid = getNumberField(&item->fields[i]); break;
			case SIGNAL   : js->signum = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",t->items[0].fields[i].name); break;
		}
	}

	return js;
}

void * deserialize_set_tag(msg_t * t) {
	jersTagSet * ts = calloc(sizeof(jersTagSet), 1);
	msg_item * item = &t->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : ts->jobid = getNumberField(&item->fields[i]); break;
			case TAG_KEY  : ts->key = getStringField(&item->fields[i]); break;
			case TAG_VALUE: ts->value = getStringField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",t->items[0].fields[i].name); break;
		}
	}

	return ts;
}

void * deserialize_del_tag(msg_t * t) {
	jersTagDel * td = calloc(sizeof(jersTagDel), 1);
	msg_item * item = &t->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case JOBID    : td->jobid = getNumberField(&item->fields[i]); break;
			case TAG_KEY  : td->key = getStringField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",t->items[0].fields[i].name); break;
		}
	}

	return td;
}

void serialize_jersJob(resp_t * r, struct job * j, int fields) {
	respAddMap(r);

	if (fields == 0 || fields & JERS_RET_JOBID)
		addIntField(r, JOBID, j->jobid);

	if (fields == 0 || fields & JERS_RET_NAME)
		addStringField(r, JOBNAME, j->jobname);

	if (fields == 0 || fields & JERS_RET_QUEUE)
		addStringField(r, QUEUENAME, j->queue->name);

	if (fields == 0 || fields & JERS_RET_STATE)
		addIntField(r, STATE, j->state);

	if (fields == 0 || fields & JERS_RET_UID)
		addIntField(r, UID, j->uid);

	if (fields == 0 || fields & JERS_RET_SUBMITTER)
		addIntField(r, SUBMITTER,j->submitter);

	if (fields == 0 || fields & JERS_RET_PRIORITY)
		addIntField(r, PRIORITY, j->priority);

	if (fields == 0 || fields & JERS_RET_SUBMITTIME)
		addIntField(r, SUBMITTIME, j->submit_time);

	if (fields == 0 || fields & JERS_RET_NICE)
		addIntField(r, NICE, j->nice);

	if (fields == 0 || fields & JERS_RET_ARGS)
		addStringArrayField(r, ARGS, j->argc, j->argv);

	if (fields == 0 || fields & JERS_RET_NODE)
		addStringField(r, NODE, j->queue->host);

	if (j->stdout && (fields == 0 || fields & JERS_RET_STDOUT))
		addStringField(r, STDOUT, j->stdout);

	if (j->stderr && (fields == 0 || fields & JERS_RET_STDERR))
		addStringField(r, STDERR, j->stderr);	

	if (j->defer_time && (fields == 0 || fields & JERS_RET_DEFERTIME))
		addIntField(r, DEFERTIME, j->defer_time);

	if (j->start_time && (fields == 0 || fields & JERS_RET_STARTTIME))
		addIntField(r, STARTTIME, j->start_time);

	if (j->finish_time && (fields == 0 || fields & JERS_RET_FINISHTIME))
		addIntField(r, FINISHTIME, j->finish_time);

	if (j->tag_count && (fields == 0 || fields & JERS_RET_TAGS))
		addStringMapField(r, TAGS, j->tag_count, j->tags);

	if (j->shell && (fields == 0 || fields & JERS_RET_SHELL))
		addStringField(r, SHELL, j->shell);	

	if (j->pre_cmd && (fields == 0 || fields & JERS_RET_PRECMD))
		addStringField(r, POSTCMD, j->pre_cmd);

	if (j->post_cmd && (fields == 0 || fields & JERS_RET_POSTCMD))
		addStringField(r, PRECMD, j->post_cmd);

	if (j->res_count && (fields == 0 || fields & JERS_RET_RESOURCES)) {
			int i;
			char ** res_strings = convertResourceToStrings(j->res_count, j->req_resources);
			addStringArrayField(r, RESOURCES, j->res_count, res_strings);

			for (i = 0; i < j->res_count; i++) {
				free(res_strings[i]);
			}

			free(res_strings);
	}

	if (j->pid && (fields == 0 || fields & JERS_RET_PID))
		addIntField(r, JOBPID, j->pid);

	addIntField(r, EXITCODE, j->exitcode);
	addIntField(r, SIGNAL, j->signal);

	if (j->pend_reason)
		addIntField(r, PENDREASON, j->pend_reason);

	if (j->fail_reason)
		addIntField(r, FAILREASON, j->fail_reason);

	respCloseMap(r);
}

int command_add_job(client * c, void * args) {
	jersJobAdd * s = args;
	struct job * j = NULL;
	struct queue * q = NULL;
	int state = 0;
	struct jobResource * resources = NULL;
	struct user * u = NULL;

	if (unlikely(server.recovery.in_progress)) {
		/* Have we already loaded this job? */
		j = findJob(s->jobid);

		if (j)
			return 0;

		print_msg(JERS_LOG_INFO, "Recovering jobid:%d\n", server.recovery.jobid);
		s->jobid = server.recovery.jobid;
		s->uid = server.recovery.uid;
	}

	/* Validate the request first up */
	if (s->queue == NULL) {
		q = server.defaultQueue;

		if (q == NULL) {
			sendError(c, JERS_ERR_NOQUEUE, "No default queue");
			return -1;
		}
	} else {
		q = findQueue(s->queue);

		if (q == NULL || q->internal_state &JERS_FLAG_DELETED) {
			sendError(c, JERS_ERR_NOQUEUE, NULL);
			return -1;
		}

		free(s->queue);
	}

	/* Have they requested a particular jobid? */
	if (s->jobid) {
		j = findJob(s->jobid);

		if (j != NULL) {
			sendError(c, JERS_ERR_JOBEXISTS, NULL);
			return -1;
		}
	}

	if (s->uid <= 0)
			s->uid = c->uid;

	if (s->uid == 0) {
		sendError(c, JERS_ERR_INVARG, "Jobs not allowed to run as root");
		return -1;
	}

	/* Validate if the requester is allowed to submit a job
	 * under another uid */
	if (server.recovery.in_progress == 0 && c->uid != s->uid) {
		if ((c->user->permissions &PERM_SETUID) == 0) {
			sendError(c, JERS_ERR_NOPERM, NULL);
			return -1;
		}
	}

	u = lookup_user(s->uid, 0);

	if (u == NULL) {
		sendError(c, JERS_ERR_INVARG, "User not valid");
		return -1;
	}

	if (s->res_count) {
		resources = convertResourceStrings(s->res_count, s->resources);

		if (resources == NULL) {
			sendError(c, JERS_ERR_NORES, NULL);
			return -1;
		}
	}

	if (s->tag_count) {
		for (int i = 0; i < s->tag_count; i++) {
			if (isprintable(s->tags[i].key) == 0) {
				free(resources);
				sendError(c, JERS_ERR_INVTAG, NULL);
				return -1;
			}

		}
	}

	/* Request looks good. Allocate a new job structure and jobid */
	j = calloc(sizeof(struct job), 1);

	if (s->jobid == 0)
		j->jobid = getNextJobID();
	else
		j->jobid = s->jobid;

	/* Fill out the job structure */
	j->jobname = s->name;
	j->queue = q;
	j->shell = s->shell;
	j->stdout = s->stdout;
	j->stderr = s->stderr;
	j->wrapper = s->wrapper;
	j->pre_cmd = s->pre_cmd;
	j->post_cmd = s->post_cmd;
	j->argc = s->argc;
	j->argv = s->argv;
	j->env_count = s->env_count;
	j->envs = s->envs;
	j->uid = s->uid;
	j->submitter = c ? c->uid : server.recovery.uid;
	j->defer_time = s->defer_time;
	j->priority = s->priority;
	j->nice = s->nice;
	j->tag_count = s->tag_count;
	j->tags = (key_val_t *)s->tags;

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

	if (server.recovery.in_progress)
		j->submit_time = server.recovery.time;
	else
		j->submit_time = time(NULL);

	/* Add it to the hashtable */
	addJob(j, state, 1);

	/* Don't respond if we are replaying a command */
	if (c == NULL)
		return 0;

	/* Return the jobid */
	resp_t response;

	if (initMessage(&response, "RESP", 1) != 0)
		return 0;

	respAddMap(&response);
	addIntField(&response, JOBID, j->jobid);
	respCloseMap(&response);

	if (sendClientMessage(c, &response) != 0)
		return 0;

	/* Populate the out jobid field */
	c->msg.jobid = j->jobid;
	c->msg.revision = 1;

	print_msg(JERS_LOG_DEBUG, "SUBMIT - JOBID %d created", j->jobid);

	server.stats.total.submitted++;

	return 0;
}

int command_get_job(client *c, void * args) {
	jersJobFilter * s = args;
	struct queue * q = NULL;
	struct job * j = NULL;

	int64_t count = 0;

	resp_t r;

	/* JobId? Just look it up and return the result */
	if (s->jobid) {
		j = findJob(s->jobid);

		if (j == NULL || (j->internal_state &JERS_FLAG_DELETED)) {
			sendError(c, JERS_ERR_NOJOB, NULL);
			return 1;
		}

		initMessage(&r, "RESP", 1);
		serialize_jersJob(&r, j, 0);
		count = 1;

	} else {
		/* If a queue filter has been provided, and its not a wildcard look it up first */
		if (s->filter_fields & JERS_FILTER_QUEUE) {

			if (strchr(s->filters.queue_name, '*') == NULL && strchr(s->filters.queue_name, '?') == NULL)
			{
				q = findQueue(s->filters.queue_name);

				if (q == NULL) {
					sendError(c, JERS_ERR_NOQUEUE, NULL);
					return -1;
				}
			}
		}

		initMessage(&r, "RESP", 1);

		respAddArray(&r);
		/* Loop through all non-deleted jobs and match against the criteria provided
		 * Add the jobs to the response as we go */

		for (j = server.jobTable; j != NULL; j = j->hh.next) {

			if (j->internal_state &JERS_FLAG_DELETED)
				continue;

			/* Try and filter on the easier criteria first */

			if (s->filter_fields & JERS_FILTER_STATE) {
				if (!(s->filters.state &j->state))
					continue;
			}

			if (s->filter_fields & JERS_FILTER_QUEUE) {
				if (q && j->queue != q) {
					continue;
				} else {
					if (matches(s->filters.queue_name, j->queue->name) != 0)
						continue;
				}
			}

			if (s->filter_fields & JERS_FILTER_UID) {
				if (s->filters.uid != j->uid)
					continue;
			}

			if (s->filter_fields & JERS_FILTER_JOBNAME) {
				if (matches(s->filters.job_name, j->jobname) != 0)
					continue;
			}

			/* Check that all the tag filters provided match the job */
			if (s->filter_fields & JERS_FILTER_TAGS) {
				int i;
				int match = 1;
				for (i = 0; i < s->filters.tag_count; i++) {
					int k;
					for (k = 0; k < j->tag_count; k++) {
						/* Match the tag first */
						if (strcmp(j->tags[k].key, s->filters.tags[i].key) == 0) {
							/* Match the value */
							if (matches(j->tags[k].value, s->filters.tags[i].value) == 0)
								break;
						}
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
			serialize_jersJob(&r, j, s->return_fields);
			count++;
		}
		respCloseArray(&r);
	}

	return sendClientMessage(c, &r);
}

int command_mod_job(client *c, void *args) {
	jersJobMod *mj = args;
	struct job * j = NULL;
	struct queue * q = NULL;
	int state = 0;
	int dirty = 0;
	int hold = 0;
	int completed = 0;

	if (mj->jobid == 0) {
		sendError(c, JERS_ERR_NOJOB, NULL);
		return 0;
	}

	j = findJob(mj->jobid);

	if (j == NULL || j->internal_state &JERS_FLAG_DELETED) {
		sendError(c, JERS_ERR_NOJOB, NULL);
		return 0;
	}

	if (unlikely(server.recovery.in_progress)) {
		if (j->revision >= server.recovery.revision) {
			print_msg(JERS_LOG_DEBUG, "Skipping recovery of job_mod job %ld rev:%ld trans rev:%ld", j->jobid, j->revision, server.recovery.revision);
			return 0;
		}
	}

	state = j->state;
	hold = (j->state == JERS_JOB_HOLDING);

	if ((j->state == JERS_JOB_COMPLETED || j->state == JERS_JOB_EXITED || j->state == JERS_JOB_UNKNOWN) && mj->restart != 1) {
		/* If a job is completed, we are allowed to run a subset of 'mod' actions.
		 * Actions that are not allowed, unless the restart flag is set:
		 *  - Hold
		 *  - Defer
		 */
		if (mj->defer_time != -1 || mj->hold != -1) {
			sendError(c, JERS_ERR_INVARG, "Unable to modify a completed job without restart flag");
			return 0;
		}

		completed = 1;
	}

	if (j->state == JERS_JOB_RUNNING || j->internal_state &JERS_FLAG_JOB_STARTED) {
		sendError(c, JERS_ERR_INVARG, "Unable to modify a running job");
		return 0;
	}

	if (mj->priority != -1) {
		if (mj->priority < JERS_JOB_MIN_PRIORITY || mj->priority > JERS_JOB_MAX_PRIORITY) {
			sendError(c, JERS_ERR_INVARG, "Invalid priority specified");
			return 0;
		}
	}

	if (mj->queue) {
		q = findQueue(mj->queue);

		if (q == NULL) {
			sendError(c, JERS_ERR_NOQUEUE, NULL);
			return 0;
		}
	}

	if (mj->name) {
		free(j->jobname);
		j->jobname = mj->name;
		dirty = 1;
	}

	if (q) {
		j->queue = q;
		dirty = 1;
	}

	if (mj->priority != -1) {
		j->priority = mj->priority;

		dirty = 1;
	}

	if (mj->defer_time != -1) {
		j->defer_time = mj->defer_time;

		dirty = 1;
	}

	if (mj->hold != -1) {
		hold = mj->hold == 0 ? 0 : 1;

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
			freeStringMap(j->tag_count, &j->tags);

		j->tag_count = mj->tag_count;
		j->tags = (key_val_t *)mj->tags;
	}

	if (mj->res_count) {
		struct jobResource * resources = NULL;

		if (j->res_count)
			free(j->req_resources);

		resources = convertResourceStrings(mj->res_count, mj->resources);

		freeStringArray(mj->res_count, &mj->resources);

		if (resources == NULL) {
			sendError(c, JERS_ERR_NORES, NULL);
			return -1;
		}

		j->req_resources = resources;
		j->res_count = mj->res_count;

		dirty = 1;
	}

	/* Need to clear some fields if this job has previously been completed */
	if (completed) {
		if (mj->restart == 1) {
			j->exitcode = 0;
			j->signal = 0;
			j->start_time = 0;
			j->finish_time = 0;
			j->fail_reason = 0;

			dirty = 1;
		}
	}

	/* Update the state to reflect any changes */
	if (j->defer_time)
		state = JERS_JOB_DEFERRED;
	else if (hold)
		state = JERS_JOB_HOLDING;
	else if (!completed)
		state = JERS_JOB_PENDING;

	if (state != j->state)
		dirty = 1;

	changeJobState(j, state, dirty);

	return sendClientReturnCode(c, "0");
}

int command_del_job(client * c, void * args) {
	jersJobDel * jd = args;
	struct job * j = NULL;

	j = findJob(jd->jobid);

	if (!j || j->internal_state &JERS_FLAG_DELETED) {
		if (unlikely(server.recovery.in_progress)) {
			print_msg(JERS_LOG_DEBUG, "Skipping deletion of non-existent %d", jd->jobid);
			return 0;
		}

		sendError(c, JERS_ERR_NOJOB, NULL);
		return 1;
	}

	j->internal_state |= JERS_FLAG_DELETED;
	changeJobState(j, 0, 0);
	server.stats.total.deleted++;

	return sendClientReturnCode(c, "0");
}

int command_sig_job(client * c, void * args) {
	jersJobSig * js = args;
	struct job * j = NULL;
	resp_t r;
	int rc = 0;

	j = findJob(js->jobid);

	if (!j || j->internal_state &JERS_FLAG_DELETED) {
		sendError(c, JERS_ERR_NOJOB, NULL);
		return 1;
	}

	if (j->state != JERS_JOB_RUNNING) {
		/* If the job state is unknown, we will set this job state to the signal provided */
		if (j->state == JERS_JOB_UNKNOWN) {
			j->state = JERS_JOB_EXITED;
			j->pid = -1;
			j->finish_time = time(NULL);
			j->signal = js->signum;
			j->exitcode = 128 + j->signal;

			return sendClientReturnCode(c, "0");
		}

		sendError(c, JERS_ERR_INVSTATE, "Job is not running");
		return 1;
	}

	/* signo == 0 wants to just test the job is running */
	if (js->signum == 0)
		return sendClientReturnCode(c, "0");

	/* Send the requested signal to the job (via the agent) */
	resp_t sig_message;
	initMessage(&sig_message, CMD_SIG_JOB, 1);

	respAddMap(&sig_message);
	addIntField(&sig_message, JOBID, js->jobid);
	addIntField(&sig_message, SIGNAL, js->signum);
	respCloseMap(&sig_message);

	sendAgentMessage(j->queue->agent, &sig_message);

	return sendClientReturnCode(c, "0");
}

int command_set_tag(client * c, void * args) {
	jersTagSet * ts = args;
	struct job * j = NULL;
	int i;

	j = findJob(ts->jobid);

	if (j == NULL) {
		sendError(c, JERS_ERR_NOJOB, NULL);
		return 1;
	}

	/* The key can only be printable characters */
	if (isprintable(ts->key) == 0) {
		sendError(c, JERS_ERR_INVTAG, NULL);
		return 1;
	}

	/* Does it have that tag? */
	for (i = 0; i < j->tag_count; i++) {
		if (strcmp(j->tags[i].key, ts->key) == 0) {
			/* Update the existing tag */
			free(j->tags[i].value);
			free(ts->key);
			j->tags[i].value = ts->value;
			break;
		}
	}

	if (i == j->tag_count) {
		/* Does not have the tag, need to add it */
		j->tags = realloc(j->tags, ++j->tag_count * sizeof(key_val_t));
		j->tags[i].key = ts->key;
		j->tags[i].value = ts->value;
	}

	setJobDirty(j);

	return sendClientReturnCode(c, "0");
}

int command_del_tag(client * c, void * args) {
	jersTagDel * td = args;
	struct job * j = NULL;
	int i;

	j = findJob(td->jobid);

	if (j == NULL) {
		sendError(c, JERS_ERR_NOJOB, NULL);
		return 1;
	}

	/* Does it have that tag? */
	for (i = 0; i < j->tag_count; i++) {
		if (strcmp(j->tags[i].key, td->key) == 0) {
			free(j->tags[i].key);
			free(j->tags[i].value);

			memmove(&j->tags[i], &j->tags[i + 1], sizeof(key_val_t) * (j->tag_count - i - 1));
			break;
		}
	}

	if (i == j->tag_count) {
		sendError(c, JERS_ERR_NOTAG, NULL);
		return 1;
	}

	j->tag_count--;

	setJobDirty(j);

	return sendClientReturnCode(c, "0");
}

void free_add_job(void * args) {
	jersJobAdd * ja = args;

	freeStringArray(ja->res_count, &ja->resources);
	free(ja);
}

void free_get_job(void * args) {
	jersJobFilter * jf = args;

	free(jf->filters.job_name);
	free(jf->filters.queue_name);
	freeStringMap(jf->filters.tag_count, (key_val_t **)&jf->filters.tags);
	freeStringArray(jf->filters.res_count, &jf->filters.resources);
	free(jf);
}

void free_mod_job(void * args) {
	jersJobMod * jm = args;

	freeStringArray(jm->res_count, &jm->resources);
	free(jm->queue);
	free(jm);
}

void free_del_job(void * args) {
	jersJobDel * jd = args;
	free(jd);
}

void free_sig_job(void * args) {
	jersJobSig * js = args;
	free(js);
}

void free_set_tag(void * args) {
	jersTagSet * ts = args;
	free(ts);
}

void free_del_tag(void * args) {
	jersTagDel * td = args;
	free(td->key);
	free(td);
}
