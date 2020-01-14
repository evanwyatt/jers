#include <stdio.h>

#include <jers_tests.h>
#include <server.h>

struct jersServer server = {0};

void clear_jobtable(void) {
	struct job *j, *tmp;

	HASH_ITER(hh, server.jobTable, j, tmp) {
		HASH_DEL(server.jobTable, j);
		free(j);
	}
}

static void test_jobids(void) {
	memset(&server, 0, sizeof(struct jersServer));

	/* Test getNextJobID() */
	server.max_jobid = 9999;
	server.start_jobid = 0;
	jobid_t previd = 0;
	int status = 0;

	/* Check we get every jobid, then an error after 9999 */
	for (int i = 0; i < 10000; i++) {
		jobid_t newid = getNextJobID();

		if (i == 9999) {
			/* We expect to have an error here */
			if (newid != 0) {
				DEBUG("Expected to get an error when jobid range full - got: %d", newid);
				status = 1;
				break;
			}
		} else {
			if (newid != previd + 1) {
				DEBUG("Newid is not old + 1. old:%d new:%d\n", previd, newid);
				status = 1;
				break;
			}
		}
		
		struct job *j = calloc(1, sizeof(struct job));
		j->jobid = newid;
		HASH_ADD_INT(server.jobTable, jobid, j);

		previd = newid;
	}

	TEST("JobID allocation - simple", status != 0);

	clear_jobtable();
	memset(&server, 0, sizeof(struct jersServer));

	server.start_jobid = 5000;
	server.max_jobid = 9999;
	previd = 5000;

	/* Same as above but wrapping around */
	for (int i = 0; i < 10000; i++) {
		jobid_t newid = getNextJobID();

		if (i == 9999) {
			/* We expect to have an error here */
			if (newid != 0) {
				DEBUG("Expected to get an error when jobid range full - got: %d instead", newid);
				status = 1;
				break;
			}
		} else {
			if (previd == server.max_jobid)
				previd = 0;

			if (newid != previd + 1) {
				DEBUG("Newid is not old + 1. old:%d new:%d\n", previd, newid);
				status = 1;
				break;
			}
		}

		struct job *j = calloc(1, sizeof(struct job));
		j->jobid = newid;
		HASH_ADD_INT(server.jobTable, jobid, j);

		previd = newid;
	}

	TEST("JobID allocation - wrap around", status != 0);

	/* Add some jobs through the table that getNextJobID will need to skip over */
	clear_jobtable();
	memset(&server, 0, sizeof(struct jersServer));

	server.start_jobid = 0;
	server.max_jobid = 9999;
	int used = 0;

	/* Not very random - Doesn't matter. */
	srand(time(NULL));

	for (int i = 0; i < 1000; i++) {
		jobid_t id = (random() % server.max_jobid) + 1;

		struct job *search = findJob(id);

		if (search == NULL) {
			struct job * j = calloc(1, sizeof(struct job));
			j->jobid = id;
			HASH_ADD_INT(server.jobTable, jobid, j);
			used++;
		}
	}

	/* Now loop through. We expect to be able to create ~8999 jobs */
	/* Check we get every jobid, then an error after 9999 */
	for (int i = 0; i < 10000 - used; i++) {
		jobid_t newid = getNextJobID();

		if (i == 9999 - used) {
			/* We expect to have an error here */
			if (newid != 0) {
				DEBUG("Expected to get an error when jobid range full - got: %d i: %d used: %d", newid, i, used);
				status = 1;
				break;
			}
		}

		struct job *j = calloc(1, sizeof(struct job));
		j->jobid = newid;
		HASH_ADD_INT(server.jobTable, jobid, j);
	}

	TEST("JobID allocation - fill in", status != 0);

	/* Should have a fully populated job table. Use findJob to check each id*/
	for (jobid_t i = 1; i <= 9999; i++) {
		struct job *j = findJob(i);

		if (j == NULL) {
			DEBUG("Failed to locate jobid %d", i);
			status = 1;
			break;
		} else if (j->jobid != i) {
			DEBUG("Job located, but ID does not match. Looking for: %d Got:%d\n", i, j->jobid);
			status = 1;
			break;
		}
	}

	TEST("findJob", status != 0);
	clear_jobtable();
}

void test_jobs(void) {
	test_jobids();


}