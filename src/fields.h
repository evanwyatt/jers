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
#ifndef _FIELDS_H
#define _FIELDS_H

#include <jers.h>
#include <buffer.h>

#define UNSET_8  INT8_MIN
#define UNSET_16 INT16_MIN
#define UNSET_32 INT32_MIN
#define UNSET_64 INT64_MIN
#define UNSET_TIME_T -1

#pragma pack(push,1)
/* This structure is aligned to match the jers_tag_t struct */
typedef struct {
	char *key;
	char *value;
} key_val_t;
#pragma pack(pop)

typedef struct {
	int number;
	int type;
	const char *name;
	size_t name_len;

	union {
		/* FIELD_TYPE_STRING */
		char *string;

		/* FIELD_TYPE_NUM */
		int64_t number;

		/* FIELD_TYPE_BOOL */
		char boolean;

		/* FIELD_TYPE_STRINGARRAY */
		struct {
			int64_t count;
			char **strings;
		} string_array;

		/* FIELD_TYPE_MAP */
		struct {
			int64_t count;
			key_val_t *keys;
		} map;
	} value;
} field;

enum field_types {
	FIELD_TYPE_BOOL = 1,
	FIELD_TYPE_STRING,
	FIELD_TYPE_NUM,
	FIELD_TYPE_STRINGARRAY,
	FIELD_TYPE_MAP,

	FIELD_TYPE_NONE = 100
};


enum field_type {
	JOBID = 0,
	JOBNAME,
	QUEUENAME,
	ARGS,
	ENVS,
	UID,
	SHELL,
	PRIORITY,
	HOLD,
	PRECMD,
	POSTCMD,
	DEFERTIME,
	SUBMITTIME,
	STARTTIME,

	TAGS,
	STATE,
	NICE,
	STDOUT,
	STDERR,
	FINISHTIME,
	NODE,
	RESOURCES,
	RETFIELDS,
	JOBPID,
	EXITCODE,
	DESC,
	JOBLIMIT,
	RESTART,

	RESNAME,
	RESCOUNT,

	STATSRUNNING,
	STATSPENDING,
	STATSDEFERRED,
	STATSHOLDING,
	STATSCOMPLETED,
	STATSEXITED,
	STATSUNKNOWN,

	STATSTOTALSUBMITTED,
	STATSTOTALSTARTED,  
	STATSTOTALCOMPLETED,
	STATSTOTALEXITED,
	STATSTOTALDELETED,
	STATSTOTALUNKNOWN,

	WRAPPER,
	COMMENT,
	RESINUSE,
	SIGNAL,
	TAG_KEY,
	TAG_VALUE,
	PENDREASON,
	FAILREASON,
	SUBMITTER,
	DEFAULT,

	USAGE_UTIME_SEC,
	USAGE_UTIME_USEC,
	USAGE_STIME_SEC,
	USAGE_STIME_USEC,
	USAGE_MAXRSS,
	USAGE_MINFLT,
	USAGE_MAJFLT,
	USAGE_INBLOCK,
	USAGE_OUBLOCK,
	USAGE_NVCSW,
	USAGE_NIVCSW,

	CLEARRES,

	NONCE,
	DATETIME,
	MSG_HMAC,
	CONNECTED,

	PID,
	PROXYDATA,

	ACCT_ID,
	ERROR,
	RETURNCODE,
	VERSION,
	ALERT,

	ENDOFFIELDS
};

typedef struct {
	unsigned char bitmap[64];	/* Bitmap of fields that have been set */
	int64_t field_count;		/* Number of fields set in 'fields' */
	int64_t field_max;
	field *fields;	/* Fields in message */
} msg_item;

typedef struct {
	char *command;
	char *error;
	int64_t version;
	int64_t item_count;
	int64_t item_max;
	msg_item *items;

	char *msg_cpy;

	/* These fields are filled in by a command so that it can be saved in the transaction journal */
	jobid_t jobid;
	int64_t revision;
} msg_t;

void sortfields(void);
void freeSortedFields(void);

const char *getFieldName(int field_no, size_t *len);

int load_message(char *json, msg_t *m);
void free_message(msg_t * msg);
int fieldtonum(const char * in);

int isFieldSet(unsigned char * bitmap, int field_no);
int setIntField(field * f, int field_no, int64_t value);

void freeStringArray(int count, char *** array);
void freeStringMap(int count, key_val_t ** keys);

char * getStringField(field * f);
int64_t getNumberField(field * f);
char getBoolField(field *f);
int64_t getStringArrayField(field *f, char *** array);
int64_t getStringMapField(field * f, key_val_t ** array);


int initRequest(buff_t *b, const char *resp_name, size_t resp_name_len, int version);
int initResponse(buff_t *b, int version);
int initResponseAlert(buff_t *b, int version, const char *alert);
int initNamedResponse(buff_t *b, const char *name, size_t name_len, int version, const char *alert);
int closeRequest(buff_t *b);
int closeResponse(buff_t *b);

#endif
