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
#include <limits.h>

#include <json.h>

#include <utlist.h>

/* Return the next free jobid.
 * 0 is returned if no ids are available */

jobid_t getNextJobID(void) {
	jobid_t id = server.start_jobid;
	jobid_t i;
	struct job * tmp_job = NULL;

	for (i = 0; i < server.max_jobid; i++) {
		if (++id > server.max_jobid)
			id = 1;

		tmp_job = findJob(id);

		if (!tmp_job) {
			server.start_jobid = id;
			return id;
		}
	}

	/* No ids available, try cleaning up some deleted jobs and try again
	 * - Only try again if we were able to clean up some jobs */
	print_msg(JERS_LOG_WARNING, "No jobids available.");
	if (cleanupJobs(5)) {
		print_msg(JERS_LOG_WARNING, "Attempting to cleanup jobs");
		return getNextJobID();
	}

	return 0;
}

/* Free a struct job entry, freeing all associated memory */

void freeJob (struct job * j) {
	for (int i = 0; i < j->tag_count; i++) {
		free(j->tags[i].key);
		free(j->tags[i].value);
	}

	free(j->tags);

	for (int i = 0; i < j->argc; i++)
		free(j->argv[i]);

	free(j->argv);

	for (int i = 0; i < j->env_count; i++)
		free(j->envs[i]);

	free(j->envs);

	if (j->res_count) {
		free(j->req_resources);
	}

	free(j->jobname);
	free(j->shell);
	free(j->pre_cmd);
	free(j->post_cmd);
	free(j->wrapper);
	free(j->stdout);
	free(j->stderr);

	free(j);
}

/* Locate the requested jobid from the job hash table */
struct job * findJob(jobid_t jobid) {
	struct job * j = NULL;
	HASH_FIND_INT(server.jobTable, &jobid, j);
	return j;
}

int cleanupJob(struct job *j) {
	/* Cleanup a single job if possible */

	/* Don't clean up jobs flagged dirty or as being flushed */
	if (j->obj.dirty || j->internal_state &JERS_FLAG_FLUSHING)
		return 1;

	stateDelJob(j);
	HASH_DEL(server.jobTable, j);

	/* If the job was a candidate for execution, clear it out of the pool */
	for (int i = 0; i < server.candidate_pool_jobs; i++) {
		if (server.candidate_pool[i] == j) {
			server.candidate_pool[i] = NULL;
			break;
		}
	}

	/* Remove the job from the indexed tag table */
	if (server.index_tag)
		delIndexTag(j);

	freeJob(j);

	server.deleted--;

	return 0;
}

/* Cleanup jobs that are marked as deleted, returning the number of jobs cleaned up
 * - Only cleanup jobs until the max_clean threshold is reached. */

int cleanupJobs(uint32_t max_clean) {
	jobid_t cleaned_up = 0;
	struct job *j, *tmp;

	if (server.deleted == 0)
		return 0;

	if (max_clean == 0)
		max_clean = 10;

	HASH_ITER(hh, server.jobTable, j, tmp) {
		if (!(j->internal_state &JERS_FLAG_DELETED))
			continue;

		/* Got a job to remove */
		cleanupJob(j);

		if (++cleaned_up >= max_clean)
			break;
	}

	return cleaned_up;
}

int addJob(struct job * j, int dirty) {
	struct job * check_job = NULL;
	int state = j->state;
	/* Save and clear the state, so we don't decrement the state on the addJob */
	j->state = 0;

	/* Does a job with this ID already exist? */
	check_job = findJob(j->jobid);

	if (check_job) {
		print_msg(JERS_LOG_WARNING, "Trying to add a duplicate jobid: %d", j->jobid);
		return 1;
	}

	HASH_ADD_INT(server.jobTable, jobid, j);

	/* Add the job to the indexed tag table, if it has the indexed tag */
	if (server.index_tag && j->tag_count) {
		for (int i = 0; i < j->tag_count; i++) {
			if (strcmp(j->tags[i].key, server.index_tag) == 0) {
				addIndexTag(j, j->tags[i].value);
				break;
			}
		}
	}

	if (j->defer_time)
		addDeferredJob(j);

	j->obj.type = JERS_OBJECT_JOB;
	changeJobState(j, state, NULL, dirty);

	return 0;
}

void deleteJob(struct job *j) {
	j->internal_state |= JERS_FLAG_DELETED;
	changeJobState(j, 0, NULL, 0);

	if (j->defer_time)
		removeDeferredJob(j);

	server.stats.total.deleted++;
	server.deleted++;
}

void markJobsUnknown(agent *a) {
	for (struct job *j = server.jobTable; j != NULL; j = j->hh.next) {
		if (j->queue->agent == a && (j->state & JERS_JOB_RUNNING || j->internal_state & JERS_FLAG_JOB_STARTED)) {
			print_msg(JERS_LOG_WARNING, "Job %d is now unknown", j->jobid);
			j->internal_state = 0;
			changeJobState(j, JERS_JOB_UNKNOWN, NULL, 1);
		}
	}
}

/* Convert a JERS object to json */
int jobToJSON(struct job *j, buff_t *buff)
{
	JSONStart(buff);
	JSONStartObject(buff, "JOB", 3);

	JSONAddInt(buff, JOBID, j->jobid);
	JSONAddString(buff, JOBNAME, j->jobname);
	JSONAddString(buff, QUEUENAME, j->queue->name);
	JSONAddInt(buff, STATE, j->state);
	JSONAddInt(buff, UID, j->uid);
	JSONAddInt(buff, SUBMITTER, j->submitter);
	JSONAddInt(buff, PRIORITY, j->priority);
	JSONAddInt(buff, SUBMITTIME, j->submit_time);
	JSONAddInt(buff, NICE, j->nice);
	JSONAddStringArray(buff, ARGS, j->argc, j->argv);
	JSONAddString(buff, NODE, j->queue->host);
	JSONAddString(buff, STDOUT, j->stdout);
	JSONAddString(buff, STDERR, j->stderr);

	if (j->defer_time)
		JSONAddInt(buff, DEFERTIME, j->defer_time);

	if (j->start_time)
		JSONAddInt(buff, STARTTIME, j->start_time);

	if (j->finish_time)
		JSONAddInt(buff, FINISHTIME, j->finish_time);

	if (j->tag_count)
		JSONAddMap(buff, TAGS, j->tag_count, j->tags);

	if (j->shell)
		JSONAddString(buff, SHELL, j->shell);

	if (j->pre_cmd)
		JSONAddString(buff, POSTCMD, j->pre_cmd);

	if (j->post_cmd)
		JSONAddString(buff, PRECMD, j->post_cmd);

	if (j->res_count)
	{
		char **res_strings = convertResourceToStrings(j->res_count, j->req_resources);
		JSONAddStringArray(buff, RESOURCES, j->res_count, res_strings);

		for (int i = 0; i < j->res_count; i++)
		{
			free(res_strings[i]);
		}

		free(res_strings);
	}

	if (j->pid)
		JSONAddInt(buff, JOBPID, j->pid);

	JSONAddInt(buff, EXITCODE, j->exitcode);
	JSONAddInt(buff, SIGNAL, j->signal);

	if (j->pend_reason)
		JSONAddInt(buff, PENDREASON, j->pend_reason);

	if (j->fail_reason)
		JSONAddInt(buff, FAILREASON, j->fail_reason);

	JSONEndObject(buff);
	JSONEnd(buff);

	return 0;
}

/* Maintain the deferred job linked list */

static inline int deferCmp(const struct job *a, const struct job *b) {
	return a->defer_time - b->defer_time;
}

void addDeferredJob(struct job *j) {
	DL_INSERT_INORDER2(server.deferred_list, j, deferCmp, deferred_prev, deferred_next);
}

void removeDeferredJob(struct job *j) {
	DL_DELETE2(server.deferred_list, j, deferred_prev, deferred_next);
}
