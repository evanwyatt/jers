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

		/* Don't clean up jobs flagged dirty or as being flushed */
		if (j->obj.dirty || j->internal_state &JERS_FLAG_FLUSHING)
			continue;

		/* Got a job to remove */
		stateDelJob(j);
		HASH_DEL(server.jobTable, j);

		/* If the job was a candidate for execution, clear it out of the pool */
		for (int i = 0; i < server.candidate_pool_jobs; i++) {
			if (server.candidate_pool[i] == j) {
				server.candidate_pool[i] = NULL;
				break;
			}
		}

		freeJob(j);
		server.deleted--;

		if (++cleaned_up >= max_clean)
			break;
	}

	return cleaned_up;
}

int addJob(struct job * j, int state, int dirty) {
	struct job * check_job = NULL;

	/* Does a job with this ID already exist? */
	check_job = findJob(j->jobid);

	if (check_job) {
		print_msg(JERS_LOG_WARNING, "Trying to add a duplicate jobid: %d", j->jobid);
		return 1;
	}

	HASH_ADD_INT(server.jobTable, jobid, j);

	j->obj.type = JERS_OBJECT_JOB;
	changeJobState(j, state, NULL, dirty);

	return 0;
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
