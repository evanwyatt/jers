/* This code is embedded it test_generateCandidatePool.c
 * Jobs are compared by:
 *      - queue priority
 *      - job priority
 *      - jobid
 *  So we try and get a broad specturm of jobs covering these states */
struct job *j;

j = calloc(1, sizeof (struct job));
j->jobid = 5;
j->queue = &q[0];
j->priority = 100;
j->state = JERS_JOB_PENDING;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.pending++;

j = calloc(1, sizeof (struct job));
j->jobid = 10;
j->queue = &q[0];
j->priority = 101;
j->state = JERS_JOB_PENDING;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.pending++;

j = calloc(1, sizeof (struct job));
j->jobid = 12;
j->queue = &q[2];
j->priority = 100;
j->state = JERS_JOB_PENDING;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.pending++;

j = calloc(1, sizeof (struct job));
j->jobid = 32;
j->queue = &q[2];
j->priority = 90;
j->state = JERS_JOB_PENDING;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.pending++;

j = calloc(1, sizeof (struct job));
j->jobid = 500;
j->queue = &q[1];
j->priority = 150;
j->state = JERS_JOB_PENDING;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.pending++;

j = calloc(1, sizeof (struct job));
j->jobid = 1020;
j->queue = &q[2];
j->priority = 100;
j->state = JERS_JOB_PENDING;

HASH_ADD_INT(server.jobTable, jobid, j);
server.stats.jobs.pending++;

/* Add some decoy jobs in there as well. (deleted and non pending) */
j = calloc(1, sizeof (struct job));
j->jobid = 38;
j->queue = &q[0];
j->priority = 100;
j->state = JERS_JOB_PENDING;

j->internal_state |= JERS_FLAG_DELETED;

HASH_ADD_INT(server.jobTable, jobid, j);

j = calloc(1, sizeof (struct job));
j->jobid = 86;
j->queue = &q[0];
j->priority = 100;
j->state = JERS_JOB_HOLDING;
j->internal_state |= JERS_FLAG_DELETED;

HASH_ADD_INT(server.jobTable, jobid, j);

j = calloc(1, sizeof (struct job));
j->jobid = 400;
j->queue = &q[0];
j->priority = 100;
j->state = JERS_JOB_HOLDING;

HASH_ADD_INT(server.jobTable, jobid, j);
