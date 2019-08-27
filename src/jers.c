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
#include <strings.h>
#include <time.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#include <argp.h>

#include <jers_cli.h>
#include <jers.h>
#include <common.h>

/* Commands + objects */

struct command commands[] = {
	{"add",    add_func},
	{"mod",    mod_func},
	{"delete", delete_func},
	{"show",   show_func},
	{"signal", signal_func},
	{NULL, NULL}
};

struct object add_objects[] = {
	{"job",      add_job},
	{"queue",    add_queue},
	{"resource", add_resource},
	{NULL, NULL}
};

struct object mod_objects[] = {
	{"job",      mod_job},
	{"queue",    mod_queue},
	{"resource", mod_resource},
	{NULL, NULL}
};

struct object delete_objects[] = {
	{"job",      delete_job},
	{"queue",    delete_queue},
	{"resource", delete_resource},
	{NULL, NULL}
};

struct object show_objects[] = {
	{"job",      show_job},
	{"queue",    show_queue},
	{"resource", show_resource},
	{NULL, NULL}
};

struct object signal_objects[] = {
	{"job", signal_job},
	{NULL, NULL}
};

int add_job(int argc, char *argv[]) {
	struct add_job_args args;
	jobid_t jobid = 0;

	if (parse_add_job(argc, argv, &args))
		return 1;

	if (args.add.argc <= 0) {
		fprintf(stderr, "add job: No commands provided\n");
		return 1;
	}

	jobid = jersAddJob(&args.add);

	if (jobid <= 0) {
		fprintf(stderr, "Failed to submit job: %s\n", jersGetErrStr(jers_errno));
		return 1;
	}

	printf("Job %d added.\n", jobid);

	return 0;
}

int add_queue(int argc, char *argv[]) {
	struct add_queue_args args;
	jersQueueAdd q;

	if (parse_add_queue(argc, argv, &args))
		return 1;

	if (args.add.name == NULL) {
		fprintf(stderr, "add queue: No queue name provided\n");
		return 1;
	}

	if (args.add.node == NULL)
		args.add.node = "localhost";

	if (jersAddQueue(&q)) {
		fprintf(stderr, "add queue: Failed to queue: %s\n", jersGetErrStr(jers_errno));
		return 1;
	}

	printf("Queue %s added.\n", q.name);

	return 0;
}

int mod_job(int argc, char *argv[]) {
	struct mod_job_args args;
	int rc = 0;

	if (parse_mod_job(argc, argv, &args)) {
		return 1;
	}

	for (int i = 0; args.jobids[i]; i++) {
		args.mod.jobid = args.jobids[i];

		if (jersModJob(&args.mod) != 0) {
			fprintf(stderr, "Failed to modify job %d: %s\n", args.jobids[i], jersGetErrStr(jers_errno));
			rc = 1;
			continue;
		}

		printf("Job %d modified.\n", args.jobids[i]);
	}

	return rc;
}

int mod_queue(int argc, char *argv[]) {
	struct mod_queue_args args;
	int rc = 0;

	if (parse_mod_queue(argc, argv, &args))
		return 1;

	if (args.queues == NULL) {
		fprintf(stderr, "mod queue: No queue name/s provided\n");
		return 1;
	}

	for (int i = 0; args.queues[i]; i++) {
		args.mod.name = args.queues[i];

		if (jersModQueue(&args.mod) != 0) {
			fprintf(stderr, "Failed to mod queue '%s': %s\n", args.queues[i], jersGetErrStr(jers_errno));
			rc = 1;
			continue;
		}

		printf("Queue %s modified.\n", args.queues[i]);
	}

	return rc;
}

int delete_job(int argc, char *argv[]) {
	struct delete_job_args args;
	int rc = 0;

	if (parse_delete_job(argc, argv, &args)) {
		return 1;
	}

	for (int i = 0; args.jobids[i]; i++) {
		if (jersDelJob(args.jobids[i]) != 0) {
			fprintf(stderr, "Failed to delete job %d: %s\n", args.jobids[i], jersGetErrStr(jers_errno));
			rc = 1;
			continue;
		}

		printf("Job %d deleted.\n", args.jobids[i]);
	}

	return rc;
}

int delete_queue(int argc, char *argv[]) {
	struct delete_queue_args args;
	int rc = 0;

	if (parse_delete_queue(argc, argv, &args))
		return 1;

	if (args.queues == NULL) {
		fprintf(stderr, "delete queue: No queue name/s provided\n");
		return 1;
	}

	for (int i = 0; args.queues[i]; i++) {
		if (jersDelQueue(args.queues[i]) != 0) {
			fprintf(stderr, "Failed to delete queue '%s': %s\n", args.queues[i], jersGetErrStr(jers_errno));
			rc = 1;
			continue;
		}

		printf("Queue %s deleted.\n", args.queues[i]);
	}

	return rc;
}

int delete_resource(int argc, char *argv[]) {
	struct del_resource_args args;
	int rc = 0;

	if (parse_del_resource(argc, argv, &args))
		return 1;

	for (int i = 0; args.resources[i]; i++) {
		if (args.verbose)
			fprintf(stderr, "Attempting to delete resource '%s'\n", args.resources[i]);

		if (jersDelResource(args.resources[i]) != 0) {
			fprintf(stderr, "Failed to delete resource '%s': %s\n", args.resources[i], jersGetErrStr(jers_errno));
			rc = 1;
			continue;
		}

		printf("Resource '%s' deleted\n", args.resources[i]);
	}

	return rc;
}

static inline char * print_state(jersJob * job) {
	char *state = "?";
	static char str[64];

	switch (job->state) {
		case JERS_JOB_COMPLETED: state = "Completed"; break;
		case JERS_JOB_EXITED:    state = "Exited"; break;
		case JERS_JOB_HOLDING:   state = "Holding"; break;
		case JERS_JOB_DEFERRED:  state = "Deferred"; break;
		case JERS_JOB_PENDING:   state = "Pending"; break;
		case JERS_JOB_RUNNING:   state = "Running"; break;
		case JERS_JOB_UNKNOWN:   state = "Unknown"; break;
	}

	strcpy(str, state);

	if (job->state == JERS_JOB_EXITED)
		sprintf(str + strlen(str), "(%d)", job->exit_code);

	return str;
}

static void _print_time(const char * str, time_t t) {
	char time_str[64];
	struct tm *tm = localtime(&t);

	strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);

	printf("%s: %s\n", str, time_str);
}

static char * getUser(uid_t uid) {
	static char username[256];
	struct passwd *pw = getpwuid(uid);

	if (pw == NULL)
		sprintf(username, "%d", uid);
	else
		strcpy(username, pw->pw_name);

	return username;
}

static char * getQueueState(int state) {
	static char state_str[64];
	snprintf(state_str, sizeof(state_str), "%s:%s", state & JERS_QUEUE_FLAG_OPEN? "Open":"Closed", state & JERS_QUEUE_FLAG_STARTED? "Started":"Stopped");
	return state_str;
}

static void print_queue(jersQueue *q, int all) {
	static int first = 1;

	if (all) {
		printf("%s\n", q->name);
		printf("------------------------\n");

	} else {
		if (first) {
			printf("Queue        Desc             Node             State          JobLimit\n");
			printf("======================================================================\n");
			first = 0;
		}

		printf("%-12s %-16.16s %-16.16s %-14.14s %-4d\n", q->name, q->desc, q->node, getQueueState(q->state), q->job_limit);
	}

	return;
}

static void print_job(jersJob *j, int all) {
	static int first = 1;

	if (all) {
		printf("Jobid: %d\n", j->jobid);
		printf("------------------------\n");
		printf("JobName: %s\n", j->jobname);
		printf("Queue: %s\n", j->queue);
		printf("User: %s(%d)\n", getUser(j->uid), j->uid);
		printf("Submitter: %s(%d)\n", getUser(j->submitter), j->submitter);
		printf("Priority: %d\n", j->priority);
		printf("Nice: %d\n", j->nice);
		printf("Node: %s\n", j->node);
		printf("Stdout: %s\n", j->stdout);
		printf("Stderr: %s\n", j->stderr);

		if (j->tag_count) {
			printf("Tags\n");
			for (int k = 0; k < j->tag_count; k++) {
				printf("Tag[%d] %s%s%s\n", k, j->tags[k].key, j->tags[k].value ? "=":"", j->tags[k].value ? j->tags[k].value : "");
			} 
		}

		printf("Arguments:");

		for (int k = 0; k < j->argc; k++) {
			if (strchr(j->argv[k], ' ') || strchr(j->argv[k], '\t'))
				printf(" '%s'", j->argv[k]);
			else
				printf(" %s", j->argv[k]);
		}

		printf("\n");

		_print_time("Submit time", j->submit_time);

		if (j->defer_time)
			_print_time("Defer time:", j->defer_time);

		if (j->start_time)
			_print_time("Start time:", j->start_time);

		if (j->finish_time) {
			_print_time("Finish time:", j->finish_time);
			printf("Exit Code: %d\n", j->exit_code);
			if (j->signal)
				printf("Signal: %d\n", j->signal);
		}

		printf("\n");
	} else {
		/* Short summary display */

		if (first) {
			printf("JobID     Queue        JobName          User       State\n");
			printf("==================================================================\n");
			first = 0;
		}

		printf("%9d %-12s %-16s %-10.10s %s\n", 
			j->jobid, j->queue, j->jobname,
			getUser(j->uid), print_state(j));
	}
}

static void print_resource(jersResource *r, int all) {
	static int first = 1;
	UNUSED(all);

	if (first) {
		printf("Resource         Count  Active  Free\n");
		printf("====================================\n");
		first = 0;
	}

	printf("%-16s %-5d  %-5d   %-5d\n", r->name, r->count, r->inuse, r->count - r->inuse);

	return;
}

static int jobSort(const void *_a, const void *_b) {
	const jersJob * a = _a;
	const jersJob * b = _b;
	int result = 0;

	result = a->state - b->state;

	if (result)
		return result;

	if (a->state == JERS_JOB_DEFERRED) {
		result = a->defer_time - b->defer_time;

		if (result)
			return result;
	}

	result = a->priority - b->priority;
	if (result)
	 return result;

	return a->jobid - b->jobid;
}

int show_job(int argc, char *argv[]) {
	struct show_job_args args;
	jersJobInfo job_info;
	int all_jobs = 0;
	int rc = 0;

	if (parse_show_job(argc, argv, &args)) {
		rc = 1;
		goto show_job_cleanup;
	}

	if (args.jobids == NULL)
		all_jobs = 1;

	for (int i = 0; all_jobs || args.jobids[i]; i++) {
		jobid_t id = all_jobs ? 0 : args.jobids[i];

		if (args.verbose)
			fprintf(stderr, "Getting info for job %d \n", id);

		if (jersGetJob(id, NULL, &job_info) != 0) {
			fprintf(stderr, "Failed to get job info for job %d: %s\n", id, jersGetErrStr(jers_errno));
			rc = 1;
			goto show_job_cleanup;
		}

		/* Sort the jobs */
		if (all_jobs) {
			qsort(job_info.jobs, job_info.count, sizeof(jersJob), jobSort);
		}

		for (int i = 0; i < job_info.count; i++)
			print_job(&job_info.jobs[i], args.all || id);

		jersFreeJobInfo(&job_info);

		if (all_jobs)
			break;
	}

show_job_cleanup:
	if (!all_jobs)
		free(args.jobids);

	return rc;
}

int signal_job(int argc, char *argv[]) {
	struct signal_job_args args;
	int rc = 0;

	if (parse_signal_job(argc, argv, &args))
		return 1;

	for (int i = 0; args.jobids[i]; i++) {
		if (jersSignalJob(args.jobids[i], args.signal) != 0) {
			fprintf(stderr, "Failed to send job %d %s: %s\n", args.jobids[i], getSignalName(args.signal), jersGetErrStr(jers_errno));
			rc = 1;
			continue;
		}

		printf("Job %d sent %s.\n", args.jobids[i], getSignalName(args.signal));
	}

	return rc;
}

int show_queue(int argc, char *argv[]) {
	jersQueueInfo qInfo;
	struct show_queue_args args;
	char *all_queues[] = {"*", NULL};

	if (parse_show_queue(argc, argv, &args))
		return 1;

	if (args.queues == NULL)
		args.queues = all_queues;

	for (int i = 0; args.queues[i]; i++) {
		if (jersGetQueue(args.queues[i], NULL, &qInfo) != 0) {
			fprintf(stderr, "Failed to get information on queue '%s': %s\n", args.queues[i], jersGetErrStr(jers_errno));
			return 1;
		}

		for (int i = 0; i < qInfo.count; i++)
			print_queue(&qInfo.queues[i], args.all);
	}

	return 0;
}

int add_resource(int argc, char *argv[]) {
	struct add_resource_args args;
	int rc = 0;

	if (parse_add_resource(argc, argv, &args)) {
		return 1;
	}

	if (args.resources == NULL) {
		fprintf(stderr, "No resources specified.\n");
		return 1;
	}

	for (int i = 0; args.resources[i]; i++) {
		/* Seperate the count from the resource name */
		char *name = args.resources[i];
		char *count_str = strchr(name, ':');
		int count = 1;

		if (count_str) {
			*count_str = '\0';
			count_str++;
			count = atoi(count_str);

			if (count == 0) {
				fprintf(stderr, "Invalid count specified on resource %s\n", name);
				rc = 1;
				continue;
			}
		}

		if (args.verbose)
			fprintf(stderr, "Adding resource '%s' count:%d\n", name, count);

		if (jersAddResource(name, count) != 0)	{
			fprintf(stderr, "Failed to add resource '%s': %s\n", name, jersGetErrStr(jers_errno));
			rc = 1;
		}
	}

	return rc;
}

int mod_resource(int argc, char *argv[]) {
	struct mod_resource_args args;
	int rc = 0;

	if (parse_mod_resource(argc, argv, &args)) {
		return 1;
	}

	for (int i = 0; args.resources[i]; i++) {
		/* Seperate the count from the resource name */
		char *name = args.resources[i];
		char *count_str = strchr(name, ':');
		int count = 1;

		if (count_str) {
			*count_str = '\0';
			count_str++;
			count = atoi(count_str);

			if (count == 0) {
				fprintf(stderr, "Invalid count specified on resource %s\n", name);
				rc = 1;
				continue;
			}
		}

		if (args.verbose)
			fprintf(stderr, "Modifying resource '%s' count:%d\n", name, count);

		if (jersModResource(name, count) != 0)	{
			fprintf(stderr, "Failed to modify resource '%s': %s\n", name, jersGetErrStr(jers_errno));
			rc = 1;
		}

		printf("Resource '%s' modified.\n", name);
	}


	return rc;
}

int show_resource(int argc, char *argv[]) {
	struct show_resource_args args;
	jersResourceInfo info;
	char *all_res = NULL;
	int rc = 0;

	if (parse_show_resource(argc, argv, &args)) {
		return 1;
	}

	if (args.resources == NULL)
		all_res = "*";

	for (int i = 0; all_res || args.resources[i]; i++) {
		if (args.verbose)
			fprintf(stderr, "Getting details for resource '%s'\n", args.resources[i]);

		if (jersGetResource(all_res ? all_res : args.resources[i], NULL, &info) != 0) {
			fprintf(stderr, "Failed to get details of resource '%s': %s\n", args.resources[i], jersGetErrStr(jers_errno));
			rc = 1;
		}

		for (int j = 0; j < info.count; j++)
			print_resource(&info.resources[j], 0);

		jersFreeResourceInfo(&info);

		if (all_res)
			break;
	}

	return rc;
}


void print_cmd_help(void) {
	//TODO: Make this help better
	printf("Expected one of the following commands:\n");
	printf(" SHOW\n");
	printf(" ADD\n");
	printf(" MODIFY\n");
	printf(" DELETE\n");
	printf(" SIGNAL\n");
	printf("\n");
}

void print_obj_help(const char * cmd) {
	//TODO: Make this help better
	printf("Expected one of the following object types for command '%s':\n", cmd);

	if (strcasecmp(cmd, "ADD") == 0) {
		printf(" JOB\n");
		printf(" QUEUE\n");
		printf(" RESOURCE\n");
	} else if (strcasecmp(cmd, "SHOW") == 0) {
		printf(" JOB\n");
		printf(" QUEUE\n");
		printf(" RESOURCE\n");
	} else if (strcasecmp(cmd, "MODIFY") == 0) {
		printf(" JOB\n");
		printf(" QUEUE\n");
		printf(" RESOURCE\n");
	} else if (strcasecmp(cmd, "DELETE") == 0) {
		printf(" JOB\n");
		printf(" QUEUE\n");
		printf(" RESOURCE\n");
	} else if (strcasecmp(cmd, "SIGNAL") == 0) {
		printf(" JOB\n");
	} else {
		printf("Unknown command provided\n");
	}

	printf("\n");
}

int run_command(int argc, char  *argv[], struct object * objects) {
	int cmd_len = strlen(argv[2]);

	for (int n = 3; n <= cmd_len; n++) {
		int i = 0;
		int matches = 0;
		struct object * obj = NULL;

		while (objects[i].object_name) {
			if (strncasecmp(objects[i].object_name, argv[2], n) == 0) {
				matches++;
				obj = &objects[i];
			}

			i++;
		}

		if (matches == 1)
			return obj->func(argc, argv);
	}

	fprintf(stderr, "Unknown object '%s' provided\n", argv[2]);
	return 1;
}

int add_func(int argc, char * argv[]) {
	return run_command(argc, argv, add_objects);
}

int mod_func(int argc, char * argv[]) {
	return run_command(argc, argv, mod_objects);
}

int delete_func(int argc, char * argv[]) {
	return run_command(argc, argv, delete_objects);
}

int show_func(int argc, char *argv[]) {
	return run_command(argc, argv, show_objects);
}

int signal_func(int argc, char *argv[]) {
	return run_command(argc, argv, signal_objects);
}

int main (int argc, char * argv[]) {
	if (argc <= 1) {
		fprintf(stderr, "No command provided.\n");
		print_cmd_help();
		return 1;
	}

	if (argc <= 2) {
		fprintf(stderr, "No object provided.\n");
		print_obj_help(argv[1]);
		return 1;
	}

	/* Allow matching at least 3 characters for the command, ie del */
	int arg_len = strlen(argv[1]);

	for (int n = 3; n <= arg_len; n++) {
		int i = 0;
		int matches = 0;
		struct command * cmd = NULL;

		while (commands[i].command_name) {
			if (strncasecmp(commands[i].command_name, argv[1], n) == 0) {
				cmd = &commands[i];
				matches++;
			}

			i++;
		}

		if (matches == 1) {
			int rc = cmd->func(argc, argv);
			jersFinish();
			return rc;
		}
	}

	fprintf(stderr, "Unknown command '%s' provided\n", argv[1]);
	print_cmd_help();
	return 1;
}
