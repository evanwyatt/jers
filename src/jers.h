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

#ifndef __JERS_H
#define __JERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t jobid_t;

#define JERS_MAJOR 1
#define JERS_MINOR 0
#define JERS_PATCH 0

#define JERS_RES_NAME_MAX 64
#define JERS_TAG_MAX 64

#define JERS_JOB_NAME_MAX 64
#define JERS_JOB_DEFAULT_PRIORITY 100
#define JERS_JOB_DEFAULT_NICE 0
#define JERS_JOB_MAX_PRIORITY 255
#define JERS_JOB_MIN_PRIORITY 0

#define JERS_QUEUE_NAME_MAX 16
#define JERS_QUEUE_DESC_MAX 128
#define JERS_QUEUE_MIN_PRIORITY 0
#define JERS_QUEUE_MAX_PRIORITY 255
#define JERS_QUEUE_DEFAULT_PRIORITY 100
#define JERS_QUEUE_MAX_LIMIT 1024
#define JERS_QUEUE_INVALID_CHARS "/\\ $"

/* Queue states are flags */
#define JERS_QUEUE_FLAG_STARTED 0x0001  // Jobs can run
#define JERS_QUEUE_FLAG_OPEN    0x0002  // Jobs can be submitted to this queue

#define JERS_QUEUE_DEFAULT_LIMIT 1
#define JERS_QUEUE_DEFAULT_STATE JERS_QUEUE_FLAG_OPEN // Open only

/* Job states are flags, so that filtering can applied with bitwise operations */
#define JERS_JOB_RUNNING   0x01
#define JERS_JOB_PENDING   0x02
#define JERS_JOB_DEFERRED  0x04
#define JERS_JOB_HOLDING   0x08
#define JERS_JOB_COMPLETED 0x10
#define JERS_JOB_EXITED    0x20
#define JERS_JOB_UNKNOWN   0x40

/* Field filter flags */
#define JERS_FILTER_JOBNAME   0x01
#define JERS_FILTER_QUEUE     0x02
#define JERS_FILTER_STATE     0x04
#define JERS_FILTER_TAGS      0x08
#define JERS_FILTER_RESOURCES 0x10
#define JERS_FILTER_UID       0x20
#define JERS_FILTER_RESNAME   0x40
#define JERS_FILTER_NODE      0x80
#define JERS_FILTER_SUBMITTER 0x0100

#define JERS_RET_JOBID      0x01
#define JERS_RET_QUEUE      0x02
#define JERS_RET_NAME       0x04
#define JERS_RET_STATE      0x08

#define JERS_RET_UID        0x20
#define JERS_RET_STDOUT     0x40
#define JERS_RET_STDERR     0x80
#define JERS_RET_TAGS       0x0100
#define JERS_RET_RESOURCES  0x0200
#define JERS_RET_PRIORITY   0x0400
#define JERS_RET_SUBMITTIME 0x0800
#define JERS_RET_DEFERTIME  0x1000
#define JERS_RET_STARTTIME  0x2000
#define JERS_RET_FINISHTIME 0x4000
#define JERS_RET_NICE       0x008000
#define JERS_RET_PRECMD     0x010000
#define JERS_RET_POSTCMD    0x020000
#define JERS_RET_ARGS       0x040000
#define JERS_RET_SHELL      0x080000
#define JERS_RET_NODE       0x100000
#define JERS_RET_SUBMITTER  0x200000
#define JERS_RET_PID        0x400000

#define JERS_RET_ALL        0x7FFFFFFFFFFFFFFF

/* Email states */
#define JERS_EMAIL_RUNNING   JERS_JOB_RUNNING
#define JERS_EMAIL_PENDING   JERS_JOB_PENDING
#define JERS_EMAIL_DEFERRED  JERS_JOB_DEFERRED
#define JERS_EMAIL_HOLDING   JERS_JOB_HOLDING
#define JERS_EMAIL_COMPLETED (JERS_JOB_COMPLETED | JERS_JOB_EXITED)
#define JERS_EMAIL_EXITED    JERS_JOB_EXITED
#define JERS_EMAIL_UNKNOWN   JERS_JOB_UNKNOWN

#define JERS_EMAIL_ALL      0x7FFFFFFF

extern int jers_errno;

enum jers_error_codes {
	JERS_ERR_OK = 0,
	JERS_ERR_NOJOB,
	JERS_ERR_NOPERM,
	JERS_ERR_NOQUEUE,
	JERS_ERR_INVARG,
	JERS_ERR_INVTAG,
	JERS_ERR_NOTAG,
	JERS_ERR_JOBEXISTS,
	JERS_ERR_RESEXISTS,
	JERS_ERR_QUEUEEXISTS,
	JERS_ERR_NOTEMPTY,
	JERS_ERR_NORES,
	JERS_ERR_INIT,
	JERS_ERR_NOCHANGE,
	JERS_ERR_INVSTATE,
	JERS_ERR_MEM,
	JERS_ERR_INVRESP,
	JERS_ERR_ERECV,
	JERS_ERR_ESEND,
	JERS_ERR_DISCONNECT,
	JERS_ERR_RESINUSE,
	JERS_ERR_READONLY,
	JERS_ERR_NOTCONN,

	JERS_ERR_UNKNOWN
};

enum jers_pend_codes {
	JERS_PEND_NOREASON = 0,
	JERS_PEND_SYSTEMFULL,
	JERS_PEND_QUEUEFULL,
	JERS_PEND_NORES,
	JERS_PEND_QUEUESTOPPED,
	JERS_PEND_AGENTDOWN,
	JERS_PEND_AGENT,
	JERS_PEND_RECON,
	JERS_PEND_READONLY,

	JERS_PEND_UNKNOWN
};

enum jers_fail_codes {
	JERS_FAIL_NOREASON = 0,
	JERS_FAIL_NOCOMPLETE,
	JERS_FAIL_PID,
	JERS_FAIL_LOGERR,
	JERS_FAIL_TMPSCRIPT,
	JERS_FAIL_START,
	JERS_FAIL_INIT,
	JERS_FAIL_SIG,

	JERS_FAIL_UNKNOWN
};

#pragma pack(push, 1)

typedef struct {
	char * key;
	char * value;
} jers_tag_t;

typedef struct {
	jobid_t jobid;

	char * name;
	char * queue;

	uid_t uid;

	char * shell;
	char * wrapper;
	char * pre_cmd;
	char * post_cmd;

	char * stdout;
	char * stderr;

	time_t defer_time;

	int nice;
	int priority;
	char hold;

	int64_t argc;
	char ** argv;

	int64_t env_count;
	char ** envs;

	int64_t tag_count;
	jers_tag_t * tags;

	int64_t res_count;
	char ** resources;

	char filler[103];
} jersJobAdd;

typedef struct {
	jobid_t jobid;
	char * name;
	char * queue;

	time_t defer_time;

	int restart;

	int nice;
	int priority;

	char hold;
	char clear_resources;
	char filler[2];

	int64_t env_count;
	char ** envs;

	int64_t tag_count;
	jers_tag_t * tags;

	int64_t res_count;
	char ** resources;

	char filler2[164];
} jersJobMod;

typedef struct {
	jobid_t jobid;
	char * jobname;
	char * queue;
	int priority;
	int state;
	int nice;
	uid_t uid;
	uid_t submitter;
	pid_t pid;

	int hold;
	int exit_code;
	int signal;

	char * shell;
	char * pre_command;
	char * post_command;

	char * stdout;
	char * stderr;

	int argc;
	char ** argv;

	char * node;
	int pend_reason;
	int fail_reason;

	time_t submit_time;
	time_t defer_time;
	time_t start_time;
	time_t finish_time;

	int tag_count;
	jers_tag_t * tags;

	int res_count;
	char ** resources;

	char filler[76];
} jersJob;

typedef struct {
	int64_t count;
	jersJob * jobs;
} jersJobInfo;

typedef struct {
	int64_t filter_fields; // Bitmask of fields populated in filters. 0 == no filters
	int64_t return_fields; // Bitmask of fields to get returned; 0 == all fields

	jobid_t jobid;

	struct {
		char * job_name;
		char * queue_name;

		char * node;

		int state;
		int64_t tag_count;
		jers_tag_t * tags;

		int64_t res_count;
		char ** resources;
		uid_t uid;
		uid_t submitter;
	} filters;
} jersJobFilter;

typedef struct {
	char *host;
} jersAgentFilter;

typedef struct {
	char * name;
	int count;
	int inuse;
} jersResource;

typedef struct {
	int64_t count;
	jersResource * resources;
} jersResourceInfo;

typedef struct {
	char * name;
	int count;

	char filler[20];
} jersResourceAdd;

typedef struct {
	char * name;
	int count;

	char filler[20];
} jersResourceMod;

typedef struct {
	char * name;
} jersResourceDel;

typedef struct {
	int64_t filter_fields;

	struct {
		char * name;
		int count;
	} filters;
} jersResourceFilter;

typedef struct {
	char *host;
	int connected;
} jersAgent;

typedef struct {
	int64_t count;
	jersAgent *agents;
} jersAgentInfo;

struct jobStats {
	int64_t running;
	int64_t pending;
	int64_t holding;
	int64_t deferred;
	int64_t completed;
	int64_t exited;
	int64_t start_pending;
	int64_t unknown;
};

typedef struct {
	char * name;
	char * desc;
	char * node;
	int job_limit;
	int state;
	int priority;
	int default_queue;

	struct jobStats stats;
} jersQueue;

typedef struct {
	int64_t count;
	jersQueue * queues;
} jersQueueInfo;

typedef struct {
	char * name;
	char * node;
	char * desc;
	int state;
	int job_limit;
	int priority;
	int default_queue;

	char filler[24];
} jersQueueAdd;

typedef struct {
	char * name;
	char * node;
	char * desc;
	int state;
	int job_limit;
	int priority;
	int default_queue;

	char filler[24];
} jersQueueMod;

typedef struct {
	int64_t filter_fields;

	struct {
		char *name;
	} filters;
} jersQueueFilter;

typedef struct {
	char * name;
} jersQueueDel;

typedef struct {
	struct jobStats current;
	struct {
		int64_t submitted;
		int64_t started;
		int64_t completed;
		int64_t exited;
		int64_t deleted;
		int64_t unknown;
	} total;
} jersStats;

#pragma pack(pop)

void jersInitJobAdd(jersJobAdd *j);
void jersInitJobMod(jersJobMod *j);

void jersInitQueueAdd(jersQueueAdd *q);
void jersInitQueueMod(jersQueueMod *q);

void jersInitResourceAdd(jersResourceAdd *r);
void jersInitResourceMod(jersResourceMod *r);

jobid_t jersAddJob(const jersJobAdd *s);
int jersModJob(const jersJobMod *j);
int jersGetJob(jobid_t id, const jersJobFilter *filter, jersJobInfo *info);
int jersDelJob(jobid_t id);
int jersSignalJob(jobid_t id, int signo);
void jersFreeJobInfo (jersJobInfo *info);

int jersSetTag(jobid_t id, const char * key, const char * value);
int jersDelTag(jobid_t id, const char * key);

int jersAddQueue(const jersQueueAdd *q);
int jersModQueue(const jersQueueMod *q);
int jersGetQueue(const char *name, const jersQueueFilter *filter, jersQueueInfo *info);
int jersDelQueue(const char *name);
void jersFreeQueueInfo(jersQueueInfo *info);

int jersAddResource(const char *name, int count);
int jersModResource(const char *name, int new_count);
int jersGetResource(const char *name, const jersResourceFilter *filter, jersResourceInfo *info);
int jersDelResource(const char *name);
void jersFreeResourceInfo(jersResourceInfo *info);

int jersGetAgents(const char *name, jersAgentInfo *info);
int jersGetStats(jersStats * s);

void jersFinish(void);
const char * jersGetErrStr(int jers_error);
const char * jersGetPendStr(int pend_reason);
const char * jersGetFailStr(int fail_reason);
#endif
