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
#ifndef _JERS_SERVER_H
#define _JERS_SERVER_H

#include <uthash.h>

#include <jers.h>
#include <common.h>
#include <resp.h>
#include <buffer.h>
#include <comms.h>
#include <fields.h>

/* Configuration defaults */

#define DEFAULT_CONFIG_FILE "/etc/jers/jers.conf"

#define DEFAULT_CONFIG_STATEDIR "/var/spool/jers/state"
#define DEFAULT_CONFIG_BACKGROUNDSAVEMS 30000
#define DEFAULT_CONFIG_LOGGINGMODE JERS_LOG_INFO
#define DEFAULT_CONFIG_EVENTFREQ 100
#define DEFAULT_CONFIG_SCHEDFREQ 500
#define DEFAULT_CONFIG_SCHEDMAX 250
#define DEFAULT_CONFIG_MAXJOBS  0
#define DEFAULT_CONFIG_MAXCLEAN 50
#define DEFAULT_CONFIG_HIGHJOBID 9999999
#define DEFAULT_CONFIG_SOCKETPATH "/var/run/jers/jers.socket"
#define DEFAULT_CONFIG_AGENTSOCKETPATH "/var/run/jers/agent.socket"
#define DEFAULT_CONFIG_FLUSHDEFER 1
#define DEFAULT_CONFIG_FLUSHDEFERMS 1000


enum job_pending_reasons {
		PEND_REASON_QUEUESTOPPED = 1,
		PEND_REASON_QUEUEFULL,
		PEND_REASON_SYSTEMFULL,
		PEND_REASON_WAITINGSTART,
		PEND_REASON_WAITINGRES,

};

typedef struct client {
	struct connectionType connection;

	msg_t msg;

	buff_t request;
	buff_t response;
	size_t response_sent;

	uid_t uid;

	struct client * next;
	struct client * prev;
} client;

typedef struct agent {
	struct connectionType connection;

	msg_t msg;

	char * host;

	/* Requests to send to this agent */
	buff_t requests;
	size_t sent;

	/* Data we've read from this agent */
	buff_t responses;

	struct agent * next;
	struct agent * prev;
} agent;

struct queue {
	char *name;
	char *desc;
	int job_limit;
	int state;
	int priority;

	char * host;
	struct agent * agent;

	struct jobStats stats;

	int dirty;
	int32_t internal_state;
	UT_hash_handle hh;
};

struct resource {
	char *name;
	int32_t count;
	int32_t in_use;

	int dirty;
	int32_t internal_state;

	UT_hash_handle hh;
};

struct jobResource {
	int32_t needed;
	struct resource * res;
};

struct job {
	jobid_t jobid;
	char * jobname;
	struct queue * queue;

	char * shell;
	char * pre_cmd;
	char * post_cmd;

	char * stdout;
	char * stderr;

	/* Command to run */
	int argc;
	char ** argv;

	int env_count;
	char ** envs;

	/* User to run job as*/
	uid_t uid;

	int nice;

	pid_t pid;
	int exitcode;
	int signal;

	int32_t state;
	int32_t internal_state;

	int pend_reason;

	int32_t priority;
	time_t submit_time;
	time_t start_time;
	time_t defer_time;
	time_t finish_time;

	int tag_count;
	char ** tags;

	int res_count;
	struct jobResource * req_resources;

	int dirty;
	UT_hash_handle hh;
};

struct event {
	void (*func)(void);
	int interval;    // Milliseconds between event triggering
	int64_t last_fire;
	struct event * next;
};

struct jersServer {
	int state_fd;
	char * state_dir;
	int state_count;

	uint32_t dirty_jobs;
	uint32_t dirty_queues;
	uint32_t dirty_resources;

	uint32_t flush_jobs;
	uint32_t flush_queues;
	uint32_t flush_resources;

	int background_save_ms;

	int logging_mode;
	//int logging_fd;;

	int shutdown;

	int event_freq;

	int sched_freq;
	int sched_max;
	int max_run_jobs;

	int max_cleanup; // Maximum deleted jobs to cleanup per cycle

	char * config_file;

	struct queue * defaultQueue;

	/* Hash Tables */
	struct job * jobTable;
	struct queue * queueTable;
	struct resource * resTable;

	struct jobStats stats;

	int candidate_recalc;

	int64_t candidate_pool_size;
	int64_t candidate_pool_jobs;
	struct job ** candidate_pool;

	char slow_logging;    // Write slow commands to a slow log
	int slow_threshold_ms; // milliseconds before a cmd is considered slow.

	jobid_t highest_jobid; // Highest possible jobID possible
//
	int event_fd;

	char * socket_path;
	struct connectionType client_connection;
	client * client_list;

	char * agent_socket_path;
	struct connectionType agent_connection;
	agent * agent_list;

	struct flush {
		pid_t pid;
		char defer;    // 0 == flush after every write. 1 == flush every flushWrite milliseconds
		int defer_ms;     // milliseconds between state file flushes
		int dirty;
	} flush;

	struct journal {
		off_t last_commit;
		//Will need to save the journal this was for, we might roll over journals?
	} journal;
};


/* The internal_state field is a bitmap of flags */
#define JERS_JOB_FLAG_DELETED  0x0001  // Job has been deleted and will be cleaned up
#define JERS_JOB_FLAG_FLUSHING 0x0002  // Job state is being flushed to disk
#define JERS_JOB_FLAG_STARTED  0x0004  // Job start message has been sent

#define INITIAL_RESPONSE_SIZE 0x1000

#define DEFAULT_REQ_SIZE 0x10000
#define MAX_EVENTS 1024

extern struct jersServer server;

jobid_t getNextJobID(void);
int addJob(struct job * j, int state, int dirty);
void freeRes(struct resource * r);

int addRes(struct resource * r, int dirty);
void freeRes(struct resource *r);

int addQueue(struct queue * q, int def, int dirty);
void freeQueue(struct queue * q);

void addClient(client * c);
void removeClient(client * c);
void addAgent(agent * a);
void removeAgent(agent * a);

void print_msg(int level, const char * format, ...);


//common.c

long getTimeMS(void);

char * print_time(struct timespec * time, int elapsed);
void timespec_diff(const struct timespec *start, const struct timespec *end, struct timespec *diff);

//config.c
void loadConfig(char * config);

//error.c
void error_die(char * msg, ...);
char * get_pend_reason(int reason);

//state.c
int stateSaveCmd(char * cmd, int cmd_len);
void stateInit(void);
int stateLoadJobs(void);
int stateLoadQueues(void);
int stateLoadResources(void);
void stateReplayJournal(void);
void stateSaveToDisk(void);

void checkJobs(void);
void releaseDeferred(void);

void changeJobState(struct job * j, int new_state, int dirty);

//auth.c
int ValidateUserAction(uid_t uid, int action);

int runCommand(client * c);
int runAgentCommand(agent * a);

void checkEvents(void);


void initEvents(void);

int cleanupJobs(int max_clean);

#endif
