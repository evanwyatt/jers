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

/* Main scheduling function
 * - This is started via an event, every server.schedfreq milliseconds.
 *   We will only attempt to release server.sched_max jobs per attempt, to
 *   avoid becoming unresponsive. */

void checkJobs(void) {
	int i = 0;
	jobid_t count = 0;
	jobid_t candidate_count = 0;
	jobid_t started_count = 0;

	struct job * j;
	struct job ** job_list = malloc(sizeof(struct job *) * 10000000); //HACK - danger!

	long start = getTimeMS();
	long end;

	time_t targetTime = time(NULL);

	struct queue * q = server.queueTable;

	for (q = server.queueTable; q != NULL; q = q->hh.next)
		q->pending_start = q->active_count;

	for (j = server.jobTable; j != NULL; j = j->hh.next) {
		count++;

		j->pend_reason = 0;

		/* Deleted jobs are cleaned up periodically. Ignore them here */
		if (j->internal_state &JERS_JOB_FLAG_DELETED)
			continue;

		if (j->state == JERS_JOB_DEFERRED && targetTime > j->defer_time) {
			/* Release a deferred job past its defer time */
			j->state = JERS_JOB_PENDING;
		}

		if (j->state != JERS_JOB_PENDING || j->internal_state &JERS_JOB_FLAG_STARTED)
			continue;

		/* If we are past here, we need to give the job a reason to be pending if we don't release it.*/

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
		if (j->queue->pending_start >= j->queue->job_limit) {
			j->pend_reason = PEND_REASON_QUEUEFULL;
			continue;
		}

		/* We can potentially start this job. */
		j->queue->pending_start++;
		job_list[candidate_count++] = j;
	}

	if (candidate_count == 0) {
		end = getTimeMS();
		free(job_list);
		return;
	}

	/* If we are already running the maximum amount of jobs, exit here.
	 * We only needed to release deferred jobs */

	if (server.stats.running > server.max_run_jobs) {
		free(job_list);
		end = getTimeMS();
		return;
	}

	/* Sort the job candidates, so we can release the highest priority, etc. */
	qsort(job_list, candidate_count, sizeof(struct job *), __comp);

	int jobs_to_start = candidate_count > server.sched_max? server.sched_max : candidate_count;

	if (jobs_to_start > server.max_run_jobs - server.stats.running)
		jobs_to_start = server.max_run_jobs - server.stats.running;

	for (i = 0; i < candidate_count; i++) {
		j = job_list[i];

		/* Check whether all the needed resources for this job are available */
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
		j->queue->active_count++;
		j->internal_state |= JERS_JOB_FLAG_STARTED;
		j->pend_reason = PEND_REASON_WAITINGSTART;

		if (++started_count >= jobs_to_start)
			break;

		//TODO: Break if we spend too much time here?
	}

	end = getTimeMS();
	print_msg(JERS_LOG_DEBUG, "*** Finished schedule poll in %ldms - Checked:%d Started:%d ****", end - start, count, started_count);

	free(job_list);
}
