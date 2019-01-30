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
#include <resp.h>
#include <buffer.h>

typedef struct {
	char * key;
	char * value;
} key_val_t;

typedef struct {
	int number;
	int type;
	const char * name;

	union {
		char * string;
		int64_t number;
		char boolean;

		struct {
			int64_t count;
			char ** strings;
		} string_array;

		struct {
			int64_t count;
			key_val_t * keys;
		} map;
	} value;
} field;

typedef struct {
	unsigned char bitmap[64];	/* Bitmap of fields that have been set */
	int64_t field_count;		/* Number of fields set in 'fields' */
	field * fields;				/* Fields in message */
} msg_item;

typedef struct {
	resp_read_t reader;
	char * command;
	char * error;
	int64_t version;
	int64_t item_count;
	msg_item * items;

	/* This field is filled in by a command so that it can be saved in the transaction journal */
	jobid_t jobid;
} msg_t;

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

	STATSTOTALSUBMITTED,
	STATSTOTALSTARTED,  
	STATSTOTALCOMPLETED,
	STATSTOTALEXITED,
	STATSTOTALDELETED,

	WRAPPER,
	COMMENT,
	RESINUSE,
	SIGNAL,
	TAG_KEY,
	TAG_VALUE,
	PENDREASON,

	ENDOFFIELDS
};
void sortfields(void);
int load_message(msg_t * msg, buff_t * buff);
void free_message(msg_t * msg, buff_t * buff);
int initMessage(resp_t * r, const char * resp_name, int version);

int fieldtonum(const char * in);

int isFieldSet(unsigned char * bitmap, int field_no);
int setIntField(field * f, int field_no, int64_t value);

void freeStringArray(int count, char *** array);
void freeStringMap(int count, key_val_t ** keys);

void addIntField(resp_t * r, int field_no, int64_t value);
void addStringField(resp_t * r, int field_no, char * value);
void addBoolField(resp_t * r, int field_no, char value);
void addStringArrayField(resp_t * r, int field_no, int count, char ** strings);
void addStringMapField(resp_t * r, int field_no, int count, key_val_t * keys);

char * getStringField(field * f);
int64_t getNumberField(field * f);
char getBoolField(field *f);
int64_t getStringArrayField(field *f, char *** array);
int64_t getStringMapField(field * f, key_val_t ** array);
#endif
