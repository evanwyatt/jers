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
#include <string.h>

#include <common.h>
#include <json.h>
#include <fields.h>

static int loadFields(char *obj, msg_t *m);
static int loadItemArray(char **json, msg_t *m);

#define FIELDNAME(x) x,CONST_STRLEN(x)
 
const field fields[] = {
	{JOBID,      FIELD_TYPE_NUM,         FIELDNAME("JOBID")},
	{JOBNAME,    FIELD_TYPE_STRING,      FIELDNAME("JOBNAME")},
	{QUEUENAME,  FIELD_TYPE_STRING,      FIELDNAME("QUEUENAME")},
	{ARGS,       FIELD_TYPE_STRINGARRAY, FIELDNAME("ARGS")},
	{ENVS,       FIELD_TYPE_STRINGARRAY, FIELDNAME("ENVS")},
	{UID,        FIELD_TYPE_NUM,         FIELDNAME("UID")},
	{SHELL,      FIELD_TYPE_STRING,      FIELDNAME("SHELL")},
	{PRIORITY,   FIELD_TYPE_NUM,         FIELDNAME("PRIORITY")},
	{HOLD,       FIELD_TYPE_BOOL,        FIELDNAME("HOLD")},
	{PRECMD,     FIELD_TYPE_STRING,      FIELDNAME("PRECMD")},
	{POSTCMD,    FIELD_TYPE_STRING,      FIELDNAME("POSTCMD")},
	{DEFERTIME,  FIELD_TYPE_NUM,         FIELDNAME("DEFERTIME")},
	{SUBMITTIME, FIELD_TYPE_NUM,         FIELDNAME("SUBMITTIME")},
	{STARTTIME,  FIELD_TYPE_NUM,         FIELDNAME("STARTIME")},

	{TAGS,       FIELD_TYPE_MAP,         FIELDNAME("TAGS")},
	{STATE,      FIELD_TYPE_NUM,         FIELDNAME("STATE")},
	{NICE,       FIELD_TYPE_NUM,         FIELDNAME("NICE")},
	{STDOUT,     FIELD_TYPE_STRING,      FIELDNAME("STDOUT")},
	{STDERR,     FIELD_TYPE_STRING,      FIELDNAME("STDERR")},
	{FINISHTIME, FIELD_TYPE_NUM,         FIELDNAME("FINISHTIME")},
	{NODE,       FIELD_TYPE_STRING,      FIELDNAME("NODE")},
	{RESOURCES,  FIELD_TYPE_STRINGARRAY, FIELDNAME("RESOURCES")},
	{RETFIELDS,  FIELD_TYPE_NUM,         FIELDNAME("RETFIELDS")},
	{JOBPID,     FIELD_TYPE_NUM,         FIELDNAME("JOBPID")},
	{EXITCODE,   FIELD_TYPE_NUM,         FIELDNAME("EXITCODE")},
	{DESC,       FIELD_TYPE_STRING,      FIELDNAME("DESC")},
	{JOBLIMIT,   FIELD_TYPE_NUM,         FIELDNAME("JOBLIMIT")},
	{RESTART,    FIELD_TYPE_BOOL,        FIELDNAME("RESTART")},
	{RESNAME,    FIELD_TYPE_STRING,      FIELDNAME("RESNAME")},
	{RESCOUNT,   FIELD_TYPE_NUM,         FIELDNAME("RESCOUNT")},
 
	{STATSRUNNING,   FIELD_TYPE_NUM, FIELDNAME("STATSRUNNING")},
	{STATSPENDING,   FIELD_TYPE_NUM, FIELDNAME("STATSPENDING")},
	{STATSDEFERRED,  FIELD_TYPE_NUM, FIELDNAME("STATSDEFERRED")},
	{STATSHOLDING,   FIELD_TYPE_NUM, FIELDNAME("STATSHOLDING")},
	{STATSCOMPLETED, FIELD_TYPE_NUM, FIELDNAME("STATSCOMPLETED")},
	{STATSEXITED,    FIELD_TYPE_NUM, FIELDNAME("STATSEXITED")},
	{STATSUNKNOWN,   FIELD_TYPE_NUM, FIELDNAME("STATSUNKNOW")},

	{STATSTOTALSUBMITTED, FIELD_TYPE_NUM, FIELDNAME("STATSTOTALSUBMITTED")},
	{STATSTOTALSTARTED,   FIELD_TYPE_NUM, FIELDNAME("STATSTOTALSTARTED")},
	{STATSTOTALCOMPLETED, FIELD_TYPE_NUM, FIELDNAME("STATSTOTALCOMPLETED")},
	{STATSTOTALEXITED,    FIELD_TYPE_NUM, FIELDNAME("STATSTOTALEXITED")},
	{STATSTOTALDELETED,   FIELD_TYPE_NUM, FIELDNAME("STATSTOTALDELETED")},
	{STATSTOTALUNKNOWN,   FIELD_TYPE_NUM, FIELDNAME("STATSTOTALUNKNOWN")},

	{WRAPPER,  FIELD_TYPE_STRING, FIELDNAME("WRAPPER")},
	{COMMENT,  FIELD_TYPE_STRING, FIELDNAME("COMMENT")},
	{RESINUSE, FIELD_TYPE_NUM,   FIELDNAME("RESINUSE")},
	{SIGNAL,   FIELD_TYPE_NUM,     FIELDNAME("SIGNAL")},

	{TAG_KEY,    FIELD_TYPE_STRING, FIELDNAME("TAG_KEY")},
	{TAG_VALUE,  FIELD_TYPE_STRING, FIELDNAME("TAG_VALUE")},
	{PENDREASON, FIELD_TYPE_NUM,   FIELDNAME("PENDREASON")},
	{FAILREASON, FIELD_TYPE_NUM,   FIELDNAME("FAILREASON")},
	{SUBMITTER,  FIELD_TYPE_NUM,    FIELDNAME("SUBMITTER")},
	{DEFAULT,    FIELD_TYPE_BOOL,   FIELDNAME("DEFAULT")},

	{USAGE_UTIME_SEC,  FIELD_TYPE_NUM, FIELDNAME("USAGE_UTIME_SEC")},
	{USAGE_UTIME_USEC, FIELD_TYPE_NUM, FIELDNAME("USAGE_UTIME_USEC")},
	{USAGE_STIME_SEC,  FIELD_TYPE_NUM, FIELDNAME("USAGE_STIME_SEC")},
	{USAGE_STIME_USEC, FIELD_TYPE_NUM, FIELDNAME("USAGE_STIME_USEC")},
	{USAGE_MAXRSS,     FIELD_TYPE_NUM, FIELDNAME("USAGE_MAXRSS")},
	{USAGE_MINFLT,     FIELD_TYPE_NUM, FIELDNAME("USAGE_MINFLT")},
	{USAGE_MAJFLT,     FIELD_TYPE_NUM, FIELDNAME("USAGE_MAJFLT")},
	{USAGE_INBLOCK,    FIELD_TYPE_NUM, FIELDNAME("USAGE_INBLOCK")},
	{USAGE_OUBLOCK,    FIELD_TYPE_NUM, FIELDNAME("USAGE_OUBLOCK")},
	{USAGE_NVCSW,      FIELD_TYPE_NUM, FIELDNAME("USAGE_NVCSW")},
	{USAGE_NIVCSW,     FIELD_TYPE_NUM, FIELDNAME("USAGE_NIVCSW")},

	{CLEARRES,         FIELD_TYPE_BOOL, FIELDNAME("CLEARRES")},

	{NONCE,    FIELD_TYPE_STRING, FIELDNAME("NONCE")},
	{DATETIME, FIELD_TYPE_NUM,    FIELDNAME("DATETIME")},
	{MSG_HMAC, FIELD_TYPE_STRING, FIELDNAME("HMAC")},

	{CONNECTED, FIELD_TYPE_BOOL, FIELDNAME("CONNECTED")},

	{PID,       FIELD_TYPE_NUM,    FIELDNAME("PID")},
	{PROXYDATA, FIELD_TYPE_STRING, FIELDNAME("PROXYDATA")},

	{ACCT_ID, FIELD_TYPE_NUM,   FIELDNAME("ACCT_ID")},
	{ERROR,   FIELD_TYPE_STRING,FIELDNAME("ERROR")},

	{RETURNCODE, FIELD_TYPE_STRING, FIELDNAME("RETURN_CODE")},
	{VERSION,    FIELD_TYPE_NUM,    FIELDNAME("VERSION")},

	{ALERT, FIELD_TYPE_STRING, FIELDNAME("ALERT")},

	{BEFORE_ADDED,    FIELD_TYPE_NUM, FIELDNAME("BEFORE_ADDED")},
	{BEFORE_STARTED,  FIELD_TYPE_NUM, FIELDNAME("BEFORE_STARTED")},
	{BEFORE_FINISHED, FIELD_TYPE_NUM, FIELDNAME("BEFORE_FINISHED")},
	{AFTER_ADDED,     FIELD_TYPE_NUM, FIELDNAME("AFTER_ADDED")},
	{AFTER_STARTED,   FIELD_TYPE_NUM, FIELDNAME("AFTER_STARTED")},
	{AFTER_FINISHED,  FIELD_TYPE_NUM, FIELDNAME("AFTER_FINISHED")},

	{REVISION, FIELD_TYPE_NUM, FIELDNAME("REVISION")},
	{TIMEOUT, FIELD_TYPE_NUM, FIELDNAME("TIMEOUT")},

	{ENDOFFIELDS, FIELD_TYPE_NUM, FIELDNAME("ENDOFFIELDS")}
};

int field_count = sizeof(fields) / sizeof(field);

static inline void setField(unsigned char * bitmap, int field_no) {
	bitmap[(field_no / 8)] |= 1 << (field_no % 8);
}

int isFieldSet(unsigned char * bitmap, int field_no) {
	return (bitmap[(field_no / 8)] >> (field_no % 8)) & 1U;
}

void freeStringArray(int count, char *** array) {
	if (array == NULL)
		return;

	for (int i = 0; i < count; i++)
		free((*array)[i]);

	free(*array);
	*array = NULL;

	return;
}

void freeStringMap(int count, key_val_t ** keys) {
	if (keys == NULL)
		return;

	for (int i = 0; i < count; i++) {
		free((*keys)[i].key);
		free((*keys)[i].value);
	}

	free(*keys);
	*keys = NULL;

	return;
}

char * getStringField(field *f) {
	char * ret = strdup(f->value.string ? f->value.string : "");
	return ret;
}

int64_t getNumberField(field *f) {
	return f->value.number;
}

char getBoolField(field *f) {
	return f->value.boolean;
}

int64_t getStringArrayField(field *f, char *** array) {
	int64_t i;

	*array = malloc(sizeof(char *) * f->value.string_array.count);

	for (i = 0; i < f->value.string_array.count; i++) {

		if (f->value.string_array.strings[i])
			(*array)[i] = strdup(f->value.string_array.strings[i]);
		else
			(*array)[i] = NULL;
	}

	return f->value.string_array.count;
}

int64_t getStringMapField(field *f, key_val_t ** array) {
	int64_t i, key_count;
	key_count = f->value.map.count;

	*array = malloc(sizeof(key_val_t) * key_count);

	for (i = 0; i < key_count; i++) {
		(*array)[i].key = strdup(f->value.map.keys[i].key);

		if (f->value.map.keys[i].value)
			(*array)[i].value = strdup(f->value.map.keys[i].value);
		else
			(*array)[i].value = NULL;
	}

	return key_count;
}

static int compfield(const void * _a, const void * _b) {
	const field * a = _a;
	const field * b = _b;

	return strcmp(a->name, b->name);
}

field * sortedFields = NULL;

void sortfields(void) {
	int num_fields = sizeof(fields)/sizeof(field);
	sortedFields = malloc(sizeof(field) * num_fields);

	memcpy(sortedFields, fields, sizeof(field) * num_fields);

	qsort(sortedFields, num_fields, sizeof(field), compfield);
}

void freeSortedFields(void) {
	free(sortedFields);
	sortedFields = NULL;
}

int fieldtonum(const char * in) {
	int i;
	static int num_fields = sizeof(fields)/sizeof(field);

	if (sortedFields) {
		field search = {0};
		search.name = in;
		field * found = bsearch(&search, sortedFields, num_fields, sizeof(field), compfield);

		if (found == NULL)
			return -1;

		return found->number;
	} else {
		for (i = 0; i < num_fields; i++) {
			if (strcmp(in, fields[i].name) == 0) return i;
		}
	}

	return -1;
}

int setIntField(field * f, int field_no, int64_t value) {
	if (field_no >= ENDOFFIELDS)
		return -1;

	f->number = field_no;
	f->name = fields[field_no].name;
	f->type = fields[field_no].type;

	if (f->type != FIELD_TYPE_NUM)
		return -1;

	f->value.number = value;

	return 1;
}

const char * getFieldName(int field_no, size_t *len) {
	if (field_no >= ENDOFFIELDS)
		return NULL;

	if (len)
		*len = fields[field_no].name_len;

	return fields[field_no].name;
}

void free_message(msg_t * msg) {
	int i,j;

	for (j = 0; j < msg->item_count; j++) {

		for (i = 0; i < msg->items[j].field_count; i++) {
			if (fields[msg->items[j].fields[i].number].type == FIELD_TYPE_STRINGARRAY) {
				free(msg->items[j].fields[i].value.string_array.strings);
			} else if (fields[msg->items[j].fields[i].number].type == FIELD_TYPE_MAP) {
				free(msg->items[j].fields[i].value.map.keys);
			}
		}

		free(msg->items[j].fields);
		msg->items[j].field_count = 0;
		msg->items[j].fields = NULL;
	}

	free(msg->msg_cpy);
	free(msg->items);
	free(msg->error);

	msg->items = NULL;
	msg->item_count = 0;
	msg->command = NULL;
	msg->version = 0;
	msg->error = NULL;
	msg->msg_cpy = NULL;
}


/* Load a message, based on a passed in JSON object.
 * The input json string is modified by this routine */
int load_message(char *json, msg_t *m)
{
	char *object;
	memset(m, 0, sizeof(msg_t));
	m->msg_cpy = strdup(json);

	object = JSONGetObject(&json);

	if (object == NULL) {
		fprintf(stderr, "Invalid JSON message received: %s\n", m->msg_cpy);
		return 1;
	}

	/* The very first token should tell us what to expect, ie "add_job" or "resp" */
	char *name = JSONGetName(&object);

	if (name == NULL) {
		fprintf(stderr, "Invalid JSON message received - No name\n");
		return 1;
	}

	/* All commands are expected to the uppercase for ease of processing later */
	uppercasestring(name);

	/* Handle error messages here */
	if (strcmp(name, "ERROR") == 0) {
		char *err_msg;

		if (JSONGetString(&object, &err_msg)) {
			fprintf(stderr, "Got an error processing an error message from: %s\n", m->msg_cpy);
			return 1;
		}

		m->error = strdup(err_msg);
		return 0;
	}

	/* Get the next nested object */
	char * cmd_object = JSONGetObject(&object);

	if (cmd_object == NULL) {
		fprintf(stderr, "Invalid JSON message received - No object\n");
		return 1;
	}

	/* Work out what to do based on the name */

	if (strcmp(name, "RESP") == 0) {
		m->command = name;

		/* Responses should contain either a return code, or a version number and an data array of items */
		while ((name = JSONGetName(&cmd_object)) != NULL) {
			uppercasestring(name);
			if (strcmp(name, "RETURN_CODE") == 0) {
				if (JSONGetString(&cmd_object, &m->command))
					return 1;

				/* Nothing else is expected when a return code is provided */
				return 0;
			}

			if (strcmp(name, "VERSION") == 0) {
				if (JSONGetNum(&cmd_object, &m->version))
					return 1;
			} else if (strcmp(name, "DATA") == 0) {
				/* "data" should be an array of items */
				if (loadItemArray(&cmd_object, m))
					return 1;
			} else if (strcmp(name, "ALERT") == 0) {
				char *alert;
				if (JSONGetString(&cmd_object, &alert))
					return 1;

				setenv(JERS_ALERT, alert, 1);
			}
		}
	} else {
		/* This should be a command, which has a version number and fields object */
		m->command = name;

		while ((name = JSONGetName(&cmd_object)) != NULL) {
			uppercasestring(name);

			if (strcmp(name, "VERSION") == 0) {
				/* Consume the version */
				if (JSONGetNum(&cmd_object, &m->version))
					return 1;
			} else if (strcmp(name, "FIELDS") == 0) {
				char *field_object = JSONGetObject(&cmd_object);

				if (field_object == NULL)
					return 1;

				if (loadFields(field_object, m))
					return 1;
			}
		}
	}

	return 0;
}

/* Load a JSON object into a msg structure */
static int loadFields(char *obj, msg_t *m) {
	char *name;
	msg_item *item;

	/* Allocate a new item structure if needed */
	if (m->item_count == m->item_max) {
		int64_t new_max = m->item_max ? m->item_max * 2 : 8;
		m->items = realloc(m->items, sizeof(msg_item) * new_max);

		if (m->items == NULL)
			return 1;

		m->item_max = new_max;
	}

	item = &m->items[m->item_count];
	item->field_max = 0;
	item->field_count = 0;
	item->fields = NULL;

	/* Load the fields in the item */
	while ((name = JSONGetName(&obj)) != NULL) {
		/* Allocate room for more fields if needed */
		if (item->field_count == item->field_max) {
			int64_t new_max = item->field_max ? item->field_max * 2 : 8;
			item->fields = realloc (item->fields, sizeof(field) * new_max);

			if (item->fields == NULL)
				return 1;

			item->field_max = new_max;
		}

		uppercasestring(name);

		field *f = &item->fields[item->field_count++];
		int field_number = fieldtonum(name);

		if (field_number < 0) {
			fprintf(stderr, "Invalid field received in command: %s\n", name);
			return 1;
		}

		setField(m->items->bitmap, field_number);
		f->number = field_number;
		f->type = fields[field_number].type;
		f->name = fields[field_number].name;

		switch (fields[field_number].type) {
			case FIELD_TYPE_NUM:
				if (JSONGetNum(&obj, &f->value.number))
					return 1;
				break;

			case FIELD_TYPE_BOOL:
				if (JSONGetBool(&obj, &f->value.boolean))
					return 1;
				break;

			case FIELD_TYPE_STRING:
				if (JSONGetString(&obj, &f->value.string))
					return 1;
				break;

			case FIELD_TYPE_STRINGARRAY:
				if ((f->value.string_array.count = JSONGetStringArray(&obj, &f->value.string_array.strings)) < 0)
					return 1;

				break;

			case FIELD_TYPE_MAP:
				if ((f->value.map.count = JSONGetMap(&obj, &f->value.map.keys)) < 0)
					return 1;

				break;

			default:
				fprintf(stderr, "Invalid field type for field %s\n", name);
				return 1;
		}

	}

	m->item_count++;

	return 0;
}

/* Load an array of items, advancing the json string */
static int loadItemArray(char **json, msg_t *m) {
	char *pos = *json;
	char *obj = NULL;

	pos = skipWhitespace(pos);

	if (*pos != '[')
		return 1;

	pos++; /* Consume the start of the array */

	/* Each item is its own object */
	while ((obj = JSONGetObject(&pos))) {
		if (loadFields(obj, m))
			return 1;

		/* Consume up until the next object */
		while (*pos != '\0' && *pos != ',' && *pos != ']') pos++;

		if (*pos == ']')
			break;

		if (*pos == ',')
			pos++;
	}

	pos++; /* Consume the end of the array */
	*json = pos;

	return 0;
}

/* Initalise a new request */

int initRequest(buff_t *b, const char *resp_name, size_t resp_name_len, int version) {
	if (buffNew(b, 1024) != 0)
		return 1;

	JSONStart(b);
	JSONStartObject(b, resp_name, resp_name_len);
	JSONAddInt(b, VERSION, version);
	JSONStartObject(b, "FIELDS", 6);

	return 0;
}

int closeRequest(buff_t *b) {
	JSONEndObject(b); /* Fields object */
	JSONEndObject(b); /* Request object */
	JSONEnd(b);

	return 0;
}

int initNamedResponse(buff_t *b, const char *name, size_t name_len, int version, const char *alert) {
	if (buffNew(b, 1024) != 0)
		return 1;

	JSONStart(b);
	JSONStartObject(b, name ? name : "RESP", name? name_len : 4);
	JSONAddInt(b, VERSION, version);

	if (alert) {
		JSONAddString(b, ALERT, alert);
	}

	JSONStartArray(b, "DATA", 4);

	return 0;
}

/* Initalise a new reponse */
int initResponseAlert(buff_t *b, int version, const char *alert) {
	return initNamedResponse(b, NULL, 0, version, alert);
}

/* Initalise a new reponse */
int initResponse(buff_t *b, int version) {
	return initNamedResponse(b, NULL, 0, version, NULL);
}

int closeResponse(buff_t *b) {
	JSONEndArray(b);  /* Data array */
	JSONEndObject(b); /* Response object */
	JSONEnd(b);

	return 0;
}