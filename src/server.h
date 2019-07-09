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

#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
 #include <sys/resource.h>

#include <uthash.h>

#include <jers.h>
#include <common.h>
#include <resp.h>
#include <buffer.h>
#include <comms.h>
#include <fields.h>
#include <logging.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#define UNUSED(x) (void)(x)

#define UNLIMITED_JOBS -1

/* Configuration defaults */

#define DEFAULT_CONFIG_FILE "/etc/jers/jers.conf"

#define DEFAULT_CONFIG_STATEDIR "/var/spool/jers/state"
#define DEFAULT_CONFIG_BACKGROUNDSAVEMS 30000
#define DEFAULT_CONFIG_LOGGINGMODE JERS_LOG_INFO
#define DEFAULT_CONFIG_EVENTFREQ 10
#define DEFAULT_CONFIG_SCHEDFREQ 25
#define DEFAULT_CONFIG_SCHEDMAX 250
#define DEFAULT_CONFIG_MAXJOBS UNLIMITED_JOBS
#define DEFAULT_CONFIG_MAXCLEAN 50
#define DEFAULT_CONFIG_MAXJOBID 9999999
#define DEFAULT_CONFIG_SOCKETPATH "/var/run/jers/jers.socket"
#define DEFAULT_CONFIG_AGENTSOCKETPATH "/var/run/jers/agent.socket"
#define DEFAULT_CONFIG_FLUSHDEFER 1
#define DEFAULT_CONFIG_FLUSHDEFERMS 5000

typedef struct _client {
	struct connectionType connection;

	msg_t msg;

	buff_t request;
	buff_t response;
	size_t response_sent;

	uid_t uid;
	struct user * user;

	struct _client * next;
	struct _client * prev;
} client;

typedef struct _agent {
	struct connectionType connection;

	msg_t msg;

	char * host;
	int recon;

	/* Requests to send to this agent */
	buff_t requests;
	size_t sent;

	/* Data we've read from this agent */
	buff_t responses;

	struct _agent * next;
	struct _agent * prev;
} agent;

struct queue {
	char *name;
	int64_t revision;
	char *desc;
	int job_limit;
	int state;
	int priority;

	char * host;
	agent * agent;

	struct jobStats stats;

	int dirty;
	int32_t internal_state;
	UT_hash_handle hh;
};

struct resource {
	char *name;
	int64_t revision;
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
	int64_t revision;
	char * jobname;
	struct queue * queue;

	char * shell;
	char * wrapper;
	char * pre_cmd;
	char * post_cmd;

	char * stdout;
	char * stderr;

	char * comment;

	/* Command to run */
	int argc;
	char ** argv;

	int env_count;
	char ** envs;

	/* User to run job as */
	uid_t uid;

	/* User who submitted the job */
	uid_t submitter;

	int nice;

	pid_t pid;
	int exitcode;
	int signal;

	struct rusage usage;

	int32_t state;
	int32_t internal_state;

	int pend_reason;
	int fail_reason;

	int32_t priority;
	time_t submit_time;
	time_t start_time;
	time_t defer_time;
	time_t finish_time;

	int tag_count;
	key_val_t * tags;

	int res_count;
	struct jobResource * req_resources;

	int dirty;
	UT_hash_handle hh;
};

struct gid_array {
	int count;
	gid_t * groups;
};

struct jersServer {
	int state_fd;
	char * state_dir;
	int state_count;

	int daemon;

	int64_t dirty_jobs;
	int64_t dirty_queues;
	int64_t dirty_resources;

	int64_t flush_jobs;
	int64_t flush_queues;
	int64_t flush_resources;

	int background_save_ms;

	char * logfile;
	int logging_mode;
	//int logging_fd;;

	/* Flags set by signal handlers */
	volatile sig_atomic_t shutdown;

	int event_freq;

	int sched_freq;
	int sched_max;
	int max_run_jobs;

	int max_cleanup; // Maximum deleted objects to cleanup per cycle

	char * config_file;

	struct queue * defaultQueue;

	/* Hash Tables */
	struct job * jobTable;
	struct queue * queueTable;
	struct resource * resTable;

	struct {
		struct jobStats jobs;
		struct {
				int64_t submitted;
				int64_t started;
				int64_t completed;
				int64_t exited;
				int64_t deleted;
				int64_t unknown;
		} total;
	} stats;

	/* Describes the groups required to run commands */
	struct {
		struct gid_array read;
		struct gid_array write;
		struct gid_array setuid;
		struct gid_array queue;
	} permissions;

	struct {
		int in_progress;
		uid_t uid;
		time_t time;
		jobid_t jobid;
		int64_t revision;
	} recovery;

	int candidate_recalc;

	int64_t candidate_pool_size;
	int64_t candidate_pool_jobs;
	struct job ** candidate_pool;

	char slow_logging;		// Write slow commands to a slow log
	int slow_threshold_ms;	// milliseconds before a cmd is considered slow.

	jobid_t max_jobid;		// Max jobID possible
	jobid_t start_jobid;	// Jobid to start allocating from

	int event_fd;

	char * socket_path;
	struct connectionType client_connection;
	client * client_list;

	char * agent_socket_path;
	struct connectionType agent_connection;
	agent * agent_list;

	struct flush {
		pid_t pid;
		char defer;		// 0 == flush after every write. 1 == flush every defer_ms milliseconds
		int defer_ms;	// milliseconds between state file flushes
		int dirty;
		time_t lastflush;
	} flush;

	struct journal {
		off_t last_commit;
		//Will need to save the journal this was for, we might roll over journals?
	} journal;
};


/* The internal_state field is a bitmap of flags */
#define JERS_FLAG_DELETED  0x0001  // Job has been deleted and will be cleaned up
#define JERS_FLAG_FLUSHING 0x0002  // Job state is being flushed to disk
#define JERS_FLAG_JOB_STARTED  0x0004  // Job start message has been sent
#define JERS_FLAG_JOB_UNKNOWN  0x0008  // Job was running/start sent to agent, agent has since disconnected.

#define INITIAL_RESPONSE_SIZE 0x1000

#define DEFAULT_REQ_SIZE 0x10000
#define MAX_EVENTS 1024

extern struct jersServer server;

void error_die(char *, ...);

jobid_t getNextJobID(void);
int addJob(struct job * j, int state, int dirty);
void freeJob(struct job * j);
struct job * findJob(jobid_t jobid);

int addRes(struct resource * r, int dirty);
void freeRes(struct resource *r);
struct resource * findResource(char * name);

int addQueue(struct queue * q, int def, int dirty);
void freeQueue(struct queue * q);
struct queue * findQueue(char * name);

void addClient(client * c);
void removeClient(client * c);
void addAgent(agent * a);
void removeAgent(agent * a);

void loadConfig(char * config);

int stateSaveCmd(uid_t uid, char * cmd, char * msg, jobid_t jobid, int64_t revision);
void stateInit(void);
int stateLoadJobs(void);
int stateLoadQueues(void);
int stateLoadResources(void);
void stateReplayJournal(void);
void stateSaveToDisk(int block);
void flush_journal(int force);

void checkJobs(void);
void releaseDeferred(void);

int stateDelJob(struct job * j);
int stateDelQueue(struct queue * q);
int stateDelResource(struct resource * r);

void setJobDirty(struct job * j);
void changeJobState(struct job * j, int new_state, struct queue *new_queue, int dirty);

int validateUserAction(client * c, int action);

int runCommand(client * c);
int runAgentCommand(agent * a);

void checkEvents(void);

void initEvents(void);

int cleanupJobs(uint32_t max_clean);
int cleanupQueues(uint32_t max_clean);
int cleanupResources(uint32_t max_clean);

void setup_listening_sockets(void);
int setReadable(struct connectionType * connection);
int setWritable(struct connectionType * connection);
void handleReadable(struct epoll_event * e);
void handleWriteable(struct epoll_event * e);

void handleClientDisconnect(client * c);
void handleAgentDisconnect(agent * a);

void sortAgentCommands(void);
void sortCommands(void);

#endif
