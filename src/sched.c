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
#include <stdlib.h>
#include <errno.h>
#include <time.h>


int __comp(const void * a_, const void * b_) {
	const struct job * a = *((struct job **) a_);
	const struct job * b = *((struct job **) b_);

	if (a->queue->priority > b->queue->priority) {
		return 1;
	}

	if (a->priority > b->priority) {
		return 1;
	}

	return (a->jobid - b->jobid);
}

void sendAgentMessage(agent * a, char * message, int64_t length) {
	buffAdd(&a->responses, message, length);
	setWritable(&a->connection);
	return;
}

void sendStartCmd(struct job * j) {
	resp_t * r = respNew();
	size_t length;

	print_msg(JERS_LOG_INFO, "Sending start message for JobID:%-7d Queue:%s QueuePriority:%d Priority:%d", j->jobid, j->queue->name, j->queue->priority, j->priority);

	respAddArray(r);
	respAddSimpleString(r, "START_JOB");
	respAddInt(r, 1);
	respAddMap(r);
	addIntField(r, JOBID, j->jobid);
	addStringField(r, JOBNAME, j->jobname);
	addStringField(r, QUEUENAME, j->queue->name);
	addIntField(r, UID, j->uid);
	addIntField(r, NICE, j->nice);

	if (j->shell)
		addStringField(r, SHELL, j->shell);

	if (j->pre_cmd)
		addStringField(r, PRECMD, j->pre_cmd);

	if (j->post_cmd)
		addStringField(r, POSTCMD, j->post_cmd);


	addStringArrayField(r, ARGS, j->argc, j->argv);

	if (j->env_count)
		addStringArrayField(r, ENVS, j->env_count, j->envs);

	if (j->stdout)
		addStringField(r, STDOUT, j->stdout);
	if (j->stderr)
		addStringField(r, STDERR, j->stderr);

	respCloseMap(r);
	respCloseArray(r);

	char * start_cmd = respFinish(r, &length);

	sendAgentMessage(j->queue->agent, start_cmd, length);

	free(start_cmd);

	return;
}

/* Check for any deferred jobs that need to be released */

void releaseDeferred(void) {
	int64_t released = 0;
	struct job * j = server.jobTable;
	time_t now = time(NULL);

	while (j) {
		if (j->state == JERS_JOB_DEFERRED && now >= j->defer_time) {
			j->state = JERS_JOB_PENDING;
			j->defer_time = 0;
			server.stats.pending++;
			server.stats.deferred--;
			released++;
		}

		j = j->hh.next;
	}

	if (released)
		server.candidate_recalc = 1;
}

/* Generate a sorted array of jobs that can be started. */

int64_t generateCandidatePool(void) {
	int64_t total_job_count = server.stats.pending + server.stats.deferred + server.stats.holding;
	int64_t candidate_count = 0;
	struct job * j;

	if (total_job_count == 0) {
		server.candidate_pool_jobs = 0;
		return server.candidate_pool_jobs;
	}

	/* Assume we will need to add every non-completed job into this pool at some point.
	 * We add some additional room so we don't need to realloc constantly */ 

	if (server.candidate_pool_size < total_job_count) {
		server.candidate_pool_size = total_job_count * 1.5;
		server.candidate_pool = realloc(server.candidate_pool, sizeof(struct job *) * server.candidate_pool_size);
	}

	/* Check each job for it's eligibility */

	for (j = server.jobTable; j != NULL; j = j->hh.next) {
		if (j->internal_state &JERS_JOB_FLAG_DELETED)
			continue;

		/* Don't include a job that isn't pending, or has already been started */
		if (j->state != JERS_JOB_PENDING || j->internal_state &JERS_JOB_FLAG_STARTED)
			continue;

		server.candidate_pool[candidate_count++] = j;
	}

	qsort(server.candidate_pool, candidate_count, sizeof(struct job *), __comp);
	server.candidate_pool_jobs = candidate_count;
	server.candidate_recalc = 0;

	return server.candidate_pool_jobs;
}

/* Main scheduling function
 * - This is started via an event, every server.schedfreq milliseconds.
 *   We will only attempt to release server.sched_max jobs per attempt, to
 *   avoid becoming unresponsive. */

void checkJobs(void) {
	int64_t i = 0;
	jobid_t checked = 0;
	jobid_t started = 0;
	struct job * j;

	long start = getTimeMS();
	long end;

	/* Don't need to do anything if we are at maximum capacity */
	if (server.stats.running >= server.max_run_jobs)
		return;

	if (server.candidate_recalc)
		generateCandidatePool();

	int64_t jobs_to_start = server.sched_max;

	if (jobs_to_start > server.candidate_pool_jobs)
		jobs_to_start = server.candidate_pool_jobs;

	if (jobs_to_start > server.max_run_jobs - server.stats.running)
		jobs_to_start = server.max_run_jobs - server.stats.running;

	for (i = 0; i < server.candidate_pool_jobs; i++) {
		checked++;
		j = server.candidate_pool[i];

		/* Started enough jobs for this iteration */
		if (started >= jobs_to_start)
			break;

		/* We can have gaps in the pool if we've removed a job */
		if (j == NULL)
			continue;

		if (j->state != JERS_JOB_PENDING || j->internal_state &JERS_JOB_FLAG_STARTED)
			continue;

		j->pend_reason = 0;

		if (server.stats.running > server.max_run_jobs) {
			j->pend_reason = PEND_REASON_SYSTEMFULL;
			continue;
		}

		/* Queue stopped? */
		if (!(j->queue->state &JERS_QUEUE_FLAG_STARTED)) {
			j->pend_reason = PEND_REASON_QUEUESTOPPED;
			continue;
		}

		/* Check the queue limit */
		if (j->queue->stats.running >= j->queue->job_limit) {
			j->pend_reason = PEND_REASON_QUEUEFULL;
			continue;
		}

		/* Resources available? */
		if (j->res_count) {
			int res_idx;
			for (res_idx = 0; res_idx < j->res_count; j++) {
				if (j->req_resources[res_idx]->needed < j->req_resources[res_idx]->res->count - j->req_resources[res_idx]->res->in_use) {
					j->pend_reason = PEND_REASON_WAITINGRES;
					break;
				}
			}

			/* Not enough of the required resources are avaiable */
			if (j->pend_reason)
				continue;
		}

		/* Increase all the needed resources */
		if (j->res_count) {
			int res_idx;
			for (res_idx = 0; res_idx < j->res_count; j++) {
				j->req_resources[res_idx]->res->in_use += j->req_resources[res_idx]->needed;
			}
		}

		sendStartCmd(j);
		j->internal_state |= JERS_JOB_FLAG_STARTED;

		j->queue->stats.running++;
		j->queue->stats.pending--;

		server.stats.running++;
		server.stats.pending--;

		started++;
	}

	end = getTimeMS();
	print_msg(JERS_LOG_DEBUG, "*** Finished schedule poll in %ldms - Checked:%d Started:%d ****", end - start, checked, started);

	return;
}
