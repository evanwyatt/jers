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

#if defined(__linux__)
#define _XOPEN_SOURCE 700
#endif

#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
#include <sys/resource.h>

#include <uthash.h>

#include <jers.h>
#include <common.h>
#include <buffer.h>
#include <comms.h>
#include <fields.h>
#include <logging.h>
#include <auth.h>
#include <agent.h>
#include <client.h>
#include <acct.h>
#include <tags.h>

#define UNUSED(x) (void)(x)

#define UNLIMITED_JOBS -1

/* Configuration defaults */

#define DEFAULT_CONFIG_FILE "/etc/jers/jers.conf"

#define DEFAULT_CONFIG_STATEDIR "/var/spool/jers/state"
#define DEFAULT_CONFIG_BACKGROUNDSAVEMS 30000
#define DEFAULT_CONFIG_LOGGINGMODE JERS_LOG_DEBUG
#define DEFAULT_CONFIG_EVENTFREQ 10
#define DEFAULT_CONFIG_SCHEDFREQ 25
#define DEFAULT_CONFIG_SCHEDMAX 250
#define DEFAULT_CONFIG_MAXJOBS UNLIMITED_JOBS
#define DEFAULT_CONFIG_MAXCLEAN 50
#define DEFAULT_CONFIG_MAXJOBID 9999999
#define DEFAULT_CONFIG_SOCKETPATH "/var/run/jers/jers.socket"
#define DEFAULT_CONFIG_AGENTSOCKETPATH "/var/run/jers/agent.socket"
#define DEFAULT_CONFIG_ACCTSOCKETPATH "/var/run/jers/accounting.socket"
#define DEFAULT_CONFIG_FLUSHDEFER 1
#define DEFAULT_CONFIG_FLUSHDEFERMS 5000
#define DEFAULT_CONFIG_EMAIL_FREQ 5000
#define DEFAULT_SLOWLOG 50 // Milliseconds

enum readonly_modes {
	READONLY_ENOSPACE = 1,
	READONLY_BGSAVE
};

enum jers_object_type {
	JERS_OBJECT_JOB = 1,
	JERS_OBJECT_QUEUE,
	JERS_OBJECT_RESOURCE
};

typedef struct _jers_object {
	int type;
	int64_t revision;
	int dirty;
} jers_object;

struct queue {
	jers_object obj;
	char *name;

	char *desc;
	int job_limit;
	int state;
	int priority;
	int nice;
	int def;

	char * host;
	agent * agent;

	int32_t internal_state;

	struct jobStats stats;

	UT_hash_handle hh;
};

struct resource {
	jers_object obj;

	char *name;
	int32_t count;
	int32_t in_use;
	int32_t internal_state;

	UT_hash_handle hh;
};

struct jobResource {
	int32_t needed;
	struct resource * res;
};

struct job {
	jers_object obj;

	jobid_t jobid;
	char * jobname;
	struct queue * queue;

	char * shell;
	char * wrapper;
	char * pre_cmd;
	char * post_cmd;

	char * stdout;
	char * stderr;

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

	/* Email instructions */
	int email_states;
	char *email_addresses;

	struct rusage usage;

	int32_t state;

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

	int32_t internal_state;

	struct indexed_tag *index_table;

	UT_hash_handle hh;
	UT_hash_handle tag_hh;
};

struct gid_array {
	int count;
	gid_t * groups;
};

struct jersServer {
	char * state_dir;
	int state_count;
	int readonly;

	int daemon;

	int secret;
	unsigned char secret_hash[SECRET_HASH_SIZE]; // Hash of secret read from config file

	int64_t dirty_jobs;
	int64_t dirty_queues;
	int64_t dirty_resources;

	int64_t flush_jobs;
	int64_t flush_queues;
	int64_t flush_resources;

	int background_save_ms;

	int email_freq_ms;

	char * logfile;
	int logging_mode;
	//int logging_fd;;

	/* Flags set by signal handlers */
	volatile sig_atomic_t shutdown;

	int event_freq;

	int sched_freq;
	int sched_max;
	int max_run_jobs;

	uint32_t max_cleanup; // Maximum deleted objects to cleanup per cycle
	uint32_t deleted;     // Number of jobs pending cleanup

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
		struct gid_array self;
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
		char *buffer;
	} recovery;
	int initalising;

	int candidate_recalc;

	int64_t candidate_pool_size;
	int64_t candidate_pool_jobs;
	struct job ** candidate_pool;

	int default_job_nice;

	int auto_cleanup;

	int slowrequest_logging;
	uint64_t slow_threshold_ms;	// milliseconds before a cmd is considered slow.

	jobid_t max_jobid;		// Max jobID possible
	jobid_t start_jobid;	// Jobid to start allocating from

	int event_fd;

	char * socket_path;
	struct connectionType client_connection;

	char * agent_socket_path;
	int agent_port;
	struct connectionType agent_connection;
	struct connectionType agent_connection_tcp;

	char *acct_socket_path;
	struct connectionType acct_connection;

	struct flush {
		pid_t pid;
		char defer;		// 0 == flush after every write. 1 == flush every defer_ms milliseconds
		int defer_ms;	// milliseconds between state file flushes
		int dirty;
		time_t lastflush;
	} flush;

	struct journal {
		int fd;
		off_t len;
		off_t limit;
		off_t size;
		off_t extend_block_size;
		off_t last_commit;
		off_t record;
		char datetime[10]; // YYYYMMDD
	} journal;

	/* A tag can be designated an 'index' tag, which adds jobs to a
	 * table of jobs in a hash table under the tag value */
	char *index_tag;
	struct indexed_tag *index_tag_table;
};

#define STATE_DIV_FACTOR 10000

#define JOURNAL_EXTEND_DEFAULT 524288 // 512kb

/* The internal_state field is a bitmap of flags */
#define JERS_FLAG_DELETED  0x0001  // Job has been deleted and will be cleaned up
#define JERS_FLAG_FLUSHING 0x0002  // Job state is being flushed to disk
#define JERS_FLAG_JOB_STARTED  0x0004  // Job start message has been sent
#define JERS_FLAG_JOB_UNKNOWN  0x0008  // Job was running/start sent to agent, agent has since disconnected.

#define INITIAL_RESPONSE_SIZE 0x1000

#define DEFAULT_REQ_SIZE 0x10000
#define MAX_EVENTS 1024

extern struct jersServer server;

jobid_t getNextJobID(void);
int addJob(struct job * j, int dirty);
void deleteJob(struct job * j);
void freeJob(struct job * j);
struct job * findJob(jobid_t jobid);

int addRes(struct resource * r, int dirty);
void freeRes(struct resource *r);
struct resource * findResource(char * name);
int checkRes(struct job *j);
void allocateRes(struct job *j);
void deallocateRes(struct job *j);

int addQueue(struct queue * q, int dirty);
void freeQueue(struct queue * q);
struct queue * findQueue(char * name);
void setDefaultQueue(struct queue *q);

void loadConfig(char * config);
void freeConfig(void);

int stateSaveCmd(uid_t uid, char * cmd, char * msg, jobid_t jobid, int64_t revision);
void stateInit(void);
int stateLoadJobs(void);
struct job * stateLoadJob(const char *filename);
int stateLoadQueues(void);
struct queue * stateLoadQueue(const char *filename);
int stateLoadResources(void);
struct resource * stateLoadResource(const char *filename);
void stateReplayJournal(void);
void stateSaveToDisk(int block);
void flush_journal(int force);

void checkJobs(void);
void releaseDeferred(void);

int stateDelJob(struct job * j);
int stateDelQueue(struct queue * q);
int stateDelResource(struct resource * r);

void changeJobState(struct job * j, int new_state, struct queue *new_queue, int dirty);
void updateObject(jers_object * obj, int dirty);

int validateUserAction(client * c, int action);

int runCommand(client * c);
int runAgentCommand(agent * a);

void checkEvents(void);
void freeEvents(void);

void initEvents(void);

int cleanupJob(struct job *j);
int cleanupJobs(uint32_t max_clean);
int cleanupQueues(uint32_t max_clean);
int cleanupResources(uint32_t max_clean);

char ** convertResourceToStrings(int res_count, struct jobResource * res);

void sortAgentCommands(void);
void sortCommands(void);

void freeSortedCommands(void);

#endif
