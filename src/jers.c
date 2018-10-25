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
#include <stdio.h>
#include <stdlib.h>

#include <jers.h>

int submit(int argc, char * argv[]) {
	jersJobAdd req;
	jersInitJobAdd(&req);
	int count = argc > 2 ? atoi(argv[2]) : 1;


	req.name = "Test jers job";
	char * args[] = {"echo", "hello", "world"};

	req.queue = "test1";
	req.uid = 759800001;
	req.argc = 3;
	req.argv = args;
	req.stdout = "/tmp/job_test.log";
int i;
for (i = 0; i < count; i++) {
	jobid_t jobid = jersAddJob(&req);
	printf("Returned: %d\n", jobid);
}
	return 0;
}

int show(int argc, char * argv[]) {
	int rc;
	int i;
	jobid_t id = 0;
	jersJobInfo j;

	if (argc > 2)
		id = atoi(argv[2]);

	rc = jersGetJob(id, NULL, &j);

	if (rc) {
		return 1;
	}

	printf("JOBID  : %d\n", j.jobs[0].jobid);
	printf("JOBNAME: %s\n", j.jobs[0].jobname);
	printf("QUEUE  : %s\n", j.jobs[0].queue);
	printf("ARGC   : %d\n", j.jobs[0].argc);

	for (i = 0; i < j.jobs[0].argc; i++)
		printf("ARGV[%d]: %s\n", i, j.jobs[0].argv[i]);

	printf("STDOUT : %s\n", j.jobs[0].stdout);
	printf("STDERR : %s\n", j.jobs[0].stderr);

	jersFreeJobInfo(&j);

	return 0;
}

int show_filter(int argc, char * argv[]) {
	jersJobFilter filter = {0};
	jersJobInfo job_ret = {0};

	filter.filter_fields |= JERS_FILTER_QUEUE;
	filter.filters.queue_name = "local_queue";

	/* Only want jobid, name and queue returned */
	filter.return_fields = JERS_RET_JOBID | JERS_RET_NAME | JERS_RET_QUEUE | JERS_RET_STATE;

	if (jersGetJob(0, &filter, &job_ret)) {
		fprintf(stderr, "Call failed\n");
		return 0;
	}

	printf("Returned %ld jobs\n", job_ret.count);

	int64_t i;
	printf("-   ID    -     Queue    -        Name          -   State   -\n");

	for (i = 0; i < job_ret.count; i++) {
		char * state;

		switch(job_ret.jobs[i].state) {
			case JERS_JOB_RUNNING:  state = "RUNNING"; break;
			case JERS_JOB_PENDING:  state = "PENDING"; break;
			case JERS_JOB_HOLDING:  state = "HOLDING"; break;
			case JERS_JOB_DEFERRED: state = "DEFERRED"; break;
			case JERS_JOB_COMPLETED: state = "COMPLETED"; break;
			case JERS_JOB_EXITED: state = "EXITED"; break;
		}



		printf("%07d  %-16s %-32s %s\n", job_ret.jobs[i].jobid, job_ret.jobs[i].queue, job_ret.jobs[i].jobname, state);
	}

	jersFreeJobInfo(&job_ret);

	return 0;
}

int main (int argc, char * argv[]) {

	if (argc < 2)
		return 1;

	if (strcasecmp(argv[1], "SUBMIT") == 0)
		return submit(argc, argv);
	else if (strcasecmp(argv[1], "SHOW") == 0)
		return show(argc, argv);
	else if(strcasecmp(argv[1], "SHOW_FILTER") == 0)
		return show_filter(argc, argv);

	return 0;
}
