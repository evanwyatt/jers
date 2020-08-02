/* This code is embedded it test_releaseDeferred */

time_t __now = time(NULL);
struct job * j;
struct queue q = {.name = "test_queue"};

j = calloc(1, sizeof (struct job));
j->jobid = 5;
j->queue = &q;
j->defer_time = __now - 120;
j->state = JERS_JOB_DEFERRED;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.deferred++;
addDeferredJob(j);

j = calloc(1, sizeof (struct job));
j->jobid = 10;
j->queue = &q;
j->defer_time = __now - 120;
j->state = JERS_JOB_DEFERRED;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.deferred++;
addDeferredJob(j);


j = calloc(1, sizeof (struct job));
j->jobid = 12;
j->queue = &q;
j->defer_time = __now + 60;
j->state = JERS_JOB_DEFERRED;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.deferred++;
addDeferredJob(j);


j = calloc(1, sizeof (struct job));
j->jobid = 32;
j->queue = &q;
j->defer_time = __now;
j->state = JERS_JOB_DEFERRED;
addDeferredJob(j);


HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.deferred++;

j = calloc(1, sizeof (struct job));
j->jobid = 500;
j->queue = &q;
j->defer_time = __now - 1;
j->state = JERS_JOB_DEFERRED;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.deferred++;
addDeferredJob(j);


j = calloc(1, sizeof (struct job));
j->jobid = 1020;
j->queue = &q;
j->defer_time = __now + 100;
j->state = JERS_JOB_DEFERRED;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.deferred++;
addDeferredJob(j);

/* Add some decoy jobs in there as well. (deleted and non pending) */
j = calloc(1, sizeof (struct job));
j->jobid = 38;
j->priority = 100;
j->state = JERS_JOB_PENDING;

j->internal_state |= JERS_FLAG_DELETED;

HASH_ADD_INT(server.jobTable, jobid, j);

j = calloc(1, sizeof (struct job));
j->jobid = 86;
j->priority = 100;
j->state = JERS_JOB_HOLDING;
j->internal_state |= JERS_FLAG_DELETED;

HASH_ADD_INT(server.jobTable, jobid, j);

j = calloc(1, sizeof (struct job));
j->jobid = 400;
j->priority = 100;
j->state = JERS_JOB_HOLDING;

HASH_ADD_INT(server.jobTable, jobid, j);
