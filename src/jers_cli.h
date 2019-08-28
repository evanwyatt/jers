#include <time.h>
#include <string.h>
#include <argp.h>

#include <jers.h>

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

struct del_resource_args {
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

int add_job(int argc, char *argv[]);
int add_queue(int argc, char *argv[]);
int add_resource(int argc, char *argv[]);

int mod_func(int argc, char *argv[]);

int mod_job(int argc, char *argv[]);
int mod_queue(int argc, char *argv[]);
int mod_resource(int argc, char *argv[]);

int delete_func(int argc, char *argv[]);

int delete_job(int argc, char *argv[]);
int delete_queue(int argc, char *argv[]);
int delete_resource(int argc, char *argv[]);

int show_func(int argc, char *argv[]);

int show_job(int argc, char *argv[]);
int show_queue(int argc, char *argv[]);
int show_resource(int argc, char *argv[]);

int show_agent(int argc, char *argv[]);

int signal_func(int argc, char *argv[]);
int signal_job(int argc, char *argv[]);

int parse_add_job(int argc, char *argv[], struct add_job_args *args);
int parse_add_queue(int argc, char * argv[], struct add_queue_args * args);
int parse_mod_job(int argc, char *argv[], struct mod_job_args *args);

int parse_show_job(int argc, char *argv[], struct show_job_args *args);
int parse_show_queue(int argc, char * argv[], struct show_queue_args * args);
int parse_mod_queue(int argc, char *argv[], struct mod_queue_args *args);

int parse_delete_queue(int argc, char * argv[], struct delete_queue_args * args);
int parse_delete_job(int argc, char * argv[], struct delete_job_args * args);

int parse_signal_job(int argc, char * argv[], struct signal_job_args * args);

int parse_add_resource(int argc, char * argv[], struct add_resource_args * args);
int parse_mod_resource(int argc, char * argv[], struct mod_resource_args * args);
int parse_del_resource(int argc, char * argv[], struct del_resource_args * args);
int parse_show_resource(int argc, char * argv[], struct show_resource_args * args);

int parse_show_agent(int argc, char * argv[], struct show_agent_args * args);
