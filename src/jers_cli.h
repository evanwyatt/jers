#include <time.h>
#include <string.h>
#include <argp.h>

#include <jers.h>

#include <assert.h>

struct command {
	char *command_name;
	int (*func)(int argc, char *argv[]);
};

struct object {
	char *object_name;
	int (*func)(int argc, char *argv[]);
};

struct add_job_args {
    int verbose;

    jersJobAdd add;
};

struct mod_job_args {
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

struct add_resource_args {
    int verbose;

    char **resources;
};

struct mod_resource_args {
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

struct mod_queue_args {
    int verbose;

    jersQueueMod mod;
    char **queues;
};

struct show_agent_args {
	int verbose;

	char *hostname;
};

int add_func(int argc, char *argv[]);
int mod_func(int argc, char *argv[]);
int delete_func(int argc, char *argv[]);
int show_func(int argc, char *argv[]);
int signal_func(int argc, char *argv[]);

#define CMD(cmd) \
	int cmd(int argc, char *argv[]); \
	int parse_##cmd(int argc, char *argv[], struct cmd##_args *args);

CMD(add_job)
CMD(show_job)
CMD(delete_job)
CMD(mod_job)
CMD(signal_job)

CMD(add_queue)
CMD(show_queue)
CMD(delete_queue)
CMD(mod_queue)

CMD(add_resource)
CMD(show_resource)
CMD(delete_resource)
CMD(mod_resource)

CMD(show_agent)
