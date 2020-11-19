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

#include <jers_cli.h>
#include <jers.h>
#include <common.h>

/* Commands + objects */

struct cmd objects[] = {
	{"job",      job_func},
	{"queue",    queue_func},
	{"resource", resource_func},
	{"agent",    agent_func},
	{"clear",    clear_func},
	{NULL, NULL}
};

struct cmd job_cmds[] = {
	{"add",    add_job},
	{"modify", modify_job},
	{"delete", delete_job},
	{"signal", signal_job},
	{"show",   show_job},
	{"start",  start_job},
	{"watch",  watch_job},
	{NULL, NULL}
};

struct cmd queue_cmds[] ={
	{"add",    add_queue},
	{"modify", modify_queue},
	{"delete", delete_queue},
	{"show",   show_queue},
	{NULL, NULL}
};

struct cmd resource_cmds[] = {
	{"add",    add_resource},
	{"modify", modify_resource},
	{"delete", delete_resource},
	{"show",   show_resource},
	{NULL, NULL}
};

struct cmd agent_cmds[] = {
	{"show", show_agent},
	{NULL, NULL}
};

struct cmd clear_cmds[] = {
	{"cache", clearcache},
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

	if (parse_add_queue(argc, argv, &args))
		return 1;

	if (args.add.name == NULL) {
		fprintf(stderr, "No queue name provided\n");
		return 1;
	}

	if (args.add.node == NULL)
		args.add.node = "localhost";

	if (jersAddQueue(&args.add)) {
		fprintf(stderr, "Failed to add queue: %s\n", jersGetErrStr(jers_errno));
		return 1;
	}

	printf("Queue %s added.\n", args.add.name);

	return 0;
}

int modify_job(int argc, char *argv[]) {
	struct modify_job_args args;
	int rc = 0;

	if (parse_modify_job(argc, argv, &args)) {
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

int modify_queue(int argc, char *argv[]) {
	struct modify_queue_args args;
	int rc = 0;

	if (parse_modify_queue(argc, argv, &args))
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
	struct delete_resource_args args;
	int rc = 0;

	if (parse_delete_resource(argc, argv, &args))
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
	static char str[128];

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
	else if (job->state == JERS_JOB_DEFERRED) {
		struct tm *tm = localtime(&job->defer_time);
		strftime(str + strlen(str), sizeof(str) - strlen(str), " - %d-%b-%Y %H:%M:%S", tm);
	}

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
	struct user *u = lookup_user( uid, 0);

	if (u == NULL)
		sprintf(username, "%d", uid);
	else
		snprintf(username, sizeof(username), "%s", u->username);

	return username;
}

static char * getQueueState(int state) {
	static char state_str[64];
	snprintf(state_str, sizeof(state_str), "%s:%s", state & JERS_QUEUE_FLAG_OPEN? "Open":"Closed", state & JERS_QUEUE_FLAG_STARTED? "Started":"Stopped");
	return state_str;
}

static void print_queue(jersQueue *q, int all) {
	static int first = 1;
	char nice[10] = "-";

	if (all) {
		printf("%s\n", q->name);
		printf("------------------------\n");

	} else {
		if (first) {
			printf("Queue         Desc             Node             State          Nice   JobLimit\n");
			printf("==============================================================================\n");
			first = 0;
		}

		if (q->nice != JERS_CLEAR_NICE)
			sprintf(nice, "%d", q->nice);

		printf("%c%-12s %-16.16s %-16.16s %-14.14s %-4.4s   %-4d\n", q->default_queue? '*':' ', q->name, q->desc ? q->desc:"", q->node, getQueueState(q->state), nice, q->job_limit);
	}

	return;
}

static void print_agent(jersAgent *a, int all) {
	static int first = 1;

	if (all) {
		printf("%s\n", a->host);
		printf("------------------------\n");

	} else {
		if (first) {
			printf("Agent Host                       Connected\n");
			printf("==========================================\n");
			first = 0;
		}

		printf("%-32.32s %s\n", a->host, a->connected ? "True" : "False");
	}

	return;
}

static void print_job(jersJob *j, int all) {
	static int first = 1;

	if (all) {
		printf("Jobid: %d\n", j->jobid);
		printf("------------------------\n");
		printf("JobName: %s\n", j->jobname);
		printf("Status: %s\n", print_state(j));
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

		if (j->env_count) {
			for (int i = 0; i < j->env_count; i++)
				printf("Env[%d]: %s\n", i, j->envs[i]);
		}

		if (j->pend_reason)
			printf("Pend Reason: %s\n", jersGetPendStr(j->pend_reason));

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
			printf("JobID     Queue        JobName          User       State                          \n");
			printf("==================================================================================\n");
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

int watch_job(int argc, char *argv[]) {
	struct watch_job_args args;
	jersJobInfo job_info;
	int rc = 0;

	if (parse_watch_job(argc, argv, &args)) {
		rc = 1;
		goto watch_job_cleanup;
	}

	if (args.jobids == NULL) {
		fprintf(stderr, "No jobid specified\n");
		return 1;
	}

	while (1) {
		/* Get the job info */
		if (jersGetJob(args.jobids[0], NULL, &job_info) != 0) {
			fprintf(stderr, "Failed to get job info for job %d: %s\n", args.jobids[0], jersGetErrStr(jers_errno));
			rc = 1;
			goto watch_job_cleanup;
		}

		/* Display it */
		print_job(&job_info.jobs[0], 1);


		/* Wait for a change in the job */
		if (jersWaitJob(args.jobids[0], job_info.jobs[0].revision, args.timeout) != 0) {
			fprintf(stderr, "Failed to wait for job: %s\n", jersGetErrStr(jers_errno));
			goto watch_job_cleanup;
		}

		jersFreeJobInfo(&job_info);
	}

watch_job_cleanup:
	free(args.jobids);

	return rc;
}

int clearcache(int argc, char *argv[]) {
	struct clearcache_args args = {0};

	if (parse_clearcache(argc, argv, &args))
		return 1;

	if (args.verbose)
		fprintf(stderr, "Sending clearcache command\n");

	if (jersClearCache() != 0) {
		fprintf(stderr, "Failed to send clearcache command: %s\n", jersGetErrStr(jers_errno));
		return 1;
	}

	if (args.verbose)
		fprintf(stderr, "Done\n");

	return 0;
}

int start_job(int argc, char *argv[]) {
	struct start_job_args args;
	int rc = 0;

	if (parse_start_job(argc, argv, &args))
		return 1;

	for (int i = 0; args.jobids[i]; i++) {
		jersJobMod mod_request;

		jersInitJobMod(&mod_request);

		mod_request.jobid = args.jobids[i];
		mod_request.hold = 0;
		mod_request.defer_time = 0;

		if (args.restart)
			mod_request.restart = 1;

		if (jersModJob(&mod_request) != 0) {
			fprintf(stderr, "Failed to start Job %d: %s\n", args.jobids[i], jersGetErrStr(jers_errno));
			rc = 1;
			continue;
		}

		printf("Job %d modified.\n", args.jobids[i]);
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

int show_agent(int argc, char *argv[]) {
	jersAgentInfo aInfo;
	struct show_agent_args args;

	if (parse_show_agent(argc, argv, &args))
		return 1;

	if (jersGetAgents(args.hostname, &aInfo) != 0) {
		fprintf(stderr, "Failed to get information on agents: %s\n", jersGetErrStr(jers_errno));
		return 1;
	}

	for (int i = 0; i < aInfo.count; i++)
		print_agent(&aInfo.agents[i], 0);

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

int modify_resource(int argc, char *argv[]) {
	struct modify_resource_args args;
	int rc = 0;

	if (parse_modify_resource(argc, argv, &args)) {
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
	printf("Expected one of the following object names:\n");
	printf(" JOB\n");
	printf(" QUEUE\n");
	printf(" RESOURCE\n");
	printf(" AGENT\n");
	printf("\n");
}

void print_obj_help(const char * cmd) {
	//TODO: Make this help better
	struct cmd (*cmd_ptr)[] = NULL;

	if (strcasecmp(cmd, "JOB") == 0)
		cmd_ptr = &job_cmds;
	else if (strcasecmp(cmd, "QUEUE") == 0)
		cmd_ptr = &queue_cmds;
	else if (strcasecmp(cmd, "RESOURCE") == 0)
		cmd_ptr = &resource_cmds;
	else if (strcasecmp(cmd, "AGENT") == 0)
		cmd_ptr = &agent_cmds;

	if (cmd_ptr) {
		printf("Expected one of the following commands for object '%s':\n", cmd);
		for (int i = 0; (*cmd_ptr)[i].name; i++)
			printf( "- %s\n", (*cmd_ptr)[i].name);
	} else {
		fprintf(stderr, "Unknown object '%s' specifed\n", cmd);
	}

	printf("\n");
}

int run_action(int argc, char  *argv[], struct cmd cmds[]) {
	if (argc < 3) {
		fprintf(stderr, "No action provided. Expected:\n");
		for (int i = 0; cmds[i].name; i++)
			fprintf(stderr, "- %s\n", cmds[i].name);

		return 1;
	}

	int cmd_len = strlen(argv[2]);

	for (int n = 3; n <= cmd_len; n++) {
		int matches = 0;
		struct cmd *cmd = NULL;

		for (int i = 0; cmds[i].name; i++) {
			if (strncasecmp(cmds[i].name, argv[2], n) == 0) {
				matches++;
				cmd = &cmds[i];
			}
		}

		if (matches == 1)
			return cmd->func(argc, argv);
	}

	fprintf(stderr, "Unknown action '%s' provided\n", argv[2]);
	return 1;
}

int job_func(int argc, char * argv[]) {
	return run_action(argc, argv, job_cmds);
}

int queue_func(int argc, char * argv[]) {
	return run_action(argc, argv, queue_cmds);
}

int resource_func(int argc, char * argv[]) {
	return run_action(argc, argv, resource_cmds);
}

int agent_func(int argc, char *argv[]) {
	return run_action(argc, argv, agent_cmds);
}

int clear_func(int argc, char *argv[]) {
	return run_action(argc, argv, clear_cmds);
}

int main (int argc, char * argv[]) {
	if (argc <= 1) {
		fprintf(stderr, "No object provided. Expected:\n");

		for (int i = 0; objects[i].name; i++)
			fprintf(stderr, "- %s\n", objects[i].name);

		print_cmd_help();
		return 1;
	}

	unsetenv(JERS_ALERT);

	/* Allow matching at least 3 characters for the command, ie del */
	int arg_len = strlen(argv[1]);

	for (int n = 3; n <= arg_len; n++) {
		int matches = 0;
		struct cmd *obj = NULL;

		for (int i = 0; objects[i].name; i++) {
			if (strncasecmp(objects[i].name, argv[1], n) == 0) {
					obj = &objects[i];
					matches++;
			}
		}

		if (matches == 1) {
			int rc = obj->func(argc, argv);
			const char *alert = getenv(JERS_ALERT);

			if (alert)
				fprintf(stderr, "*** ALERT: %s\n", alert);

			jersFinish();
			return rc;
		}
	}

	fprintf(stderr, "Unknown command '%s' provided\n", argv[1]);
	print_cmd_help();
	return 1;
}
