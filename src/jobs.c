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
 *  * 0 is returned if no ids are avaiable */

jobid_t getNextJobID(void) {
	static jobid_t start_jobid = 0;
	jobid_t i;

	jobid_t id = start_jobid;
	struct job * tmp_job = NULL;

	for (i = 0; i < server.highest_jobid; i++) {
		if (++id >= server.highest_jobid)
			id = 1;
		HASH_FIND_INT(server.jobTable, &id, tmp_job);

		if (!tmp_job) {
			start_jobid = id;
			return id;
		}
	}

	/* No ids available, try cleaning up some deleted jobs and try again
	 * - Only try again if we were able to clean up some jobs */
	print_msg(JERS_LOG_WARNING, "No jobids available. Attemping to cleanup jobs.");
	if (cleanupJobs(5))
		return getNextJobID();


	return 0;
}

/* Free a struct job entry, freeing all associated memory */

void free_job (struct job * j) {

	if (j->tags) {
		char ** tag = j->tags;

		while (*tag) {
			free(*tag);
			tag++;
		}

		free(j->tags);
	}

	if (j->res_count) {
		free(j->req_resources);
	}

	free(j->argv);
	free(j);
}


/* Cleanup jobs that are marked as deleted, returning the number of jobs cleaned up
 * - Only cleanup jobs until the max_clean threshold is reached. */

int cleanupJobs(int max_clean) {
	jobid_t cleaned_up = 0;
	struct job * j;

	if (max_clean == 0)
		max_clean = 10;

	for (j = server.jobTable; j != NULL; j = j->hh.next) {
		if (!(j->internal_state &JERS_JOB_FLAG_DELETED))
			continue;

		/* Don't clean up jobs flagged dirty or as being flushed */
		if (j->dirty || j->internal_state &JERS_JOB_FLAG_FLUSHING)
			continue;

		/* Got a job to remove */
		stateDelJob(j);
		HASH_DEL(server.jobTable, j);

		if (++cleaned_up >= max_clean)
			break;
	}

	return cleaned_up;
}

int addJob(struct job * j, int state, int dirty) {
	struct job * check_job = NULL;

	/* Does a job with this ID already exist? */
	HASH_FIND_INT(server.jobTable, &j->jobid, check_job);

	if (check_job) {
		print_msg(JERS_LOG_WARNING, "Trying to add a duplicate jobid: %d", j->jobid);
		return 1;
	}

	HASH_ADD_INT(server.jobTable, jobid, j);

	changeJobState(j, state, dirty);

	return 0;
}
