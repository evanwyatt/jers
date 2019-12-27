#ifndef __JERS_CLI_H
#define __JERS_CLI_H

#include <time.h>
#include <string.h>

#include <jers.h>

#include <assert.h>

struct cmd {
	char *name;
	int (*func)(int argc, char *argv[]);
};

struct add_job_args {
    int verbose;

    jersJobAdd add;
};

struct modify_job_args {
    int verbose;

    jobid_t *jobids;
    jersJobMod mod;
};

struct add_queue_args {
	int verbose;

    jersQueueAdd add;
};

struct show_job_args {
    int verbose;
    int all;

    jobid_t *jobids;
};

struct show_queue_args {
    int verbose;
    int all;

    char **queues;
};

struct delete_queue_args {
    int verbose;

    char **queues;
};

struct delete_job_args {
    int verbose;

    jobid_t *jobids;
};

struct signal_job_args {
	int verbose;

	int signal;
	jobid_t *jobids;
};

struct start_job_args {
    int verbose;
    
    int restart;
    jobid_t *jobids;
};

struct add_resource_args {
    int verbose;

    char **resources;
};

struct modify_resource_args {
    int verbose;

    char **resources;
};

struct show_resource_args {
    int verbose;

    char **resources;
};

struct delete_resource_args {
    int verbose;

    char **resources;
};

struct modify_queue_args {
    int verbose;

    jersQueueMod mod;
    char **queues;
};

struct show_agent_args {
	int verbose;

	char *hostname;
};

int job_func(int argc, char *argv[]);
int queue_func(int argc, char *argv[]);
int resource_func(int argc, char *argv[]);
int agent_func(int argc, char *argv[]);

#define CMD(__cmd) \
	int __cmd(int argc, char *argv[]); \
	int parse_##__cmd(int argc, char *argv[], struct __cmd##_args *args);

CMD(add_job)
CMD(show_job)
CMD(delete_job)
CMD(modify_job)
CMD(signal_job)
CMD(start_job)

CMD(add_queue)
CMD(show_queue)
CMD(delete_queue)
CMD(modify_queue)

CMD(add_resource)
CMD(show_resource)
CMD(delete_resource)
CMD(modify_resource)

CMD(show_agent)

#endif