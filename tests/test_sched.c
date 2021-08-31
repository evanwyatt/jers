#include <stdio.h>

#include <jers_tests.h>
#include <server.h>

void generateCandidatePool(void);
void releaseDeferred(void);
void clear_jobtable(void);

int test_generateCandidatePool(void) {
	int status = 0;
	/* The expected order of jobs after the pool is generated */
	jobid_t expected_order[] = {500, 12, 1020, 32, 10, 5};
	int64_t expected_count = 6;

	/* Need some queues for queue sorting priorities */
	struct queue q[] = {
		{.name = "test_queue1", .priority = 1, .job_limit = 10}, // Third
		{.name = "test_queue2", .priority = 10, .job_limit = 5}, // First
		{.name = "test_queue3", .priority = 5, .job_limit = 1}	 // Second
	};

/* Add a whole bunch of jobs */
#include <_test_gen_jobs.c>

	generateCandidatePool();

	if (server.candidate_pool_jobs != expected_count) {
		DEBUG("Incorrect number of jobs in candidate pool. Expected:%ld Got:%ld\n", expected_count, server.candidate_pool_jobs);
		status = 1;
		goto end;
	}

	for (int i = 0; i < server.candidate_pool_jobs; i++) {
		if (expected_order[i] != server.candidate_pool[i]->jobid) {
			printf("Candidate pool is not in the expected order. Expected:\n");
			for (int j = 0; j < expected_count; j++) {
				printf("[%d] = %d\n", j, expected_order[j]);
			}

			printf("Got:\n");
			for (int j = 0; j < expected_count; j++) {
				printf("[%d] = %d QueuePriority:%d Priority:%d\n", j,
					   server.candidate_pool[j]->jobid,
					   server.candidate_pool[j]->queue->priority,
					   server.candidate_pool[j]->priority);
			}

			status = 1;
			goto end;
		}
	}

end:
	clear_jobtable();
	return status;
}

int test_releaseDeferred(void) {
	int status = 0;
	jobid_t expected_pend[] = {5, 10, 32, 500};
	int64_t expected_pend_count = 4;

	jobid_t expected_defer[] = {10, 5, 500, 32, 12, 1020, 1001};
	int64_t expected_defer_count = 7;

/* Load the jobs */
#include <_test_gen_jobs2.c>

	int64_t count = 0;

	/* Check the jobs loaded into the deferred linked list in order */
	for (struct job *j = server.deferred_list; j; j = j->deferred_next)
	{
		if (j->deferred_next) {
			if (j->deferred_next->defer_time < j->defer_time) {
				printf("Unexpected defer time order:\n");
				printf("Current - jobid:%u defer_time:%ld\n", j->jobid, j->defer_time);
				printf("Next    - jobid:%u defer_time:%ld\n", j->deferred_next->jobid, j->deferred_next->defer_time);
				status = 1;
				goto end;
			}
		}

		count++;
	}

	if (count != expected_defer_count)
	{
		printf("Unexpected number of jobs in deferred list, expected %ld, got %ld\n", expected_pend_count, count);
		status = 1;
		goto end;
	}

	releaseDeferred();

	/* After releasing the jobs, they should have been removed from the linked list */
	for (struct job *j = server.deferred_list; j; j = j->deferred_next)
	{
		/* Check if it should be in the list at all */
		for (int i = 0; i < expected_pend_count; i++) {
			if (expected_pend[i] == j->jobid)
			{
				printf("Error - Released job %d was in the deferred list\n", j->jobid);
				status = 1;
				goto end;
			}
		}

		if (j->deferred_next) {
			if (j->deferred_next->defer_time < j->defer_time) {
				printf("Unexpected defer time order (after release):\n");
				printf("Current - jobid:%u defer_time:%ld\n", j->jobid, j->defer_time);
				printf("Next    - jobid:%u defer_time:%ld\n", j->deferred_next->jobid, j->deferred_next->defer_time);
				status = 1;
				goto end;
			}
		}
	}

	/* Check the expected jobs are now the only ones pending */
	for (j = server.jobTable; j; j = j->hh.next) {
		if (j->state != JERS_JOB_PENDING || j->internal_state & JERS_FLAG_DELETED)
			continue;

		int i;
		for (i = 0; i < expected_defer_count; i++) {
			if (expected_defer[i] == j->jobid)
				break;
		}

		if (i >= expected_defer_count) {
			printf("Got unexpected pending job %d\n", j->jobid);
			status = 1;
			goto end;
		}
	}

	/* Check the queue counts are correct */

end:
	clear_jobtable();
	return status;
}

void test_sched(void) {
	TEST("generateCandidatePool", test_generateCandidatePool());
	TEST("releaseDeferred", test_releaseDeferred());
}
