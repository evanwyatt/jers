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

#include <resp.h>
#include <fields.h>

field fields[] = {
	{JOBID,      RESP_TYPE_INT,        "JOBID"},
	{JOBNAME,    RESP_TYPE_BLOBSTRING, "JOBNAME"},
	{QUEUENAME,  RESP_TYPE_BLOBSTRING, "QUEUENAME"},
	{ARGS,       RESP_TYPE_ARRAY,      "ARGS"},
	{ENVS,       RESP_TYPE_ARRAY,      "ENVS"},
	{UID,        RESP_TYPE_INT,        "UID"},
	{SHELL,      RESP_TYPE_BLOBSTRING, "SHELL"},
	{PRIORITY,   RESP_TYPE_INT,        "PRIORITY"},
	{HOLD,       RESP_TYPE_BOOL,       "HOLD"},
	{PRECMD,     RESP_TYPE_BLOBSTRING, "PRECMD"},
	{POSTCMD,    RESP_TYPE_BLOBSTRING, "POSTCMD"},
	{DEFERTIME,  RESP_TYPE_INT,        "DEFERTIME"},
	{SUBMITTIME, RESP_TYPE_INT,        "SUBMITTIME"},
	{STARTTIME,  RESP_TYPE_INT,        "STARTIME"},

	{TAGS,       RESP_TYPE_MAP,        "TAGS"},
	{STATE,      RESP_TYPE_INT,        "STATE"},
	{NICE,       RESP_TYPE_INT,        "NICE"},
	{STDOUT,     RESP_TYPE_BLOBSTRING, "STDOUT"},
	{STDERR,     RESP_TYPE_BLOBSTRING, "STDERR"},
	{FINISHTIME, RESP_TYPE_INT,        "FINISHTIME"},
	{NODE,       RESP_TYPE_BLOBSTRING, "NODE"},
	{RESOURCES,  RESP_TYPE_ARRAY,      "RESOURCES"},
	{RETFIELDS,  RESP_TYPE_INT,        "RETFIELDS"},
	{JOBPID,     RESP_TYPE_INT,        "JOBPID"},
	{EXITCODE,   RESP_TYPE_INT,        "EXITCODE"},
	{DESC,       RESP_TYPE_BLOBSTRING, "DESC"},
	{JOBLIMIT,   RESP_TYPE_INT,        "JOBLIMIT"},
	{RESTART,    RESP_TYPE_BOOL,       "RESTART"},
	{RESNAME,    RESP_TYPE_BLOBSTRING, "RESNAME"},
	{RESCOUNT,   RESP_TYPE_INT,        "RESCOUNT"},

	{STATSRUNNING,   RESP_TYPE_INT, "STATSRUNNING"},
	{STATSPENDING,   RESP_TYPE_INT, "STATSPENDING"},
	{STATSDEFERRED,  RESP_TYPE_INT, "STATSDEFERRED"},
	{STATSHOLDING,   RESP_TYPE_INT, "STATSHOLDING"},
	{STATSCOMPLETED, RESP_TYPE_INT, "STATSCOMPLETED"},
	{STATSEXITED,    RESP_TYPE_INT, "STATSEXITED"},
	{STATSUNKNOWN,   RESP_TYPE_INT, "STATSUNKNOW"},

	{STATSTOTALSUBMITTED, RESP_TYPE_INT, "STATSTOTALSUBMITTED"},
	{STATSTOTALSTARTED,   RESP_TYPE_INT, "STATSTOTALSTARTED"},
	{STATSTOTALCOMPLETED, RESP_TYPE_INT, "STATSTOTALCOMPLETED"},
	{STATSTOTALEXITED,    RESP_TYPE_INT, "STATSTOTALEXITED"},
	{STATSTOTALDELETED,   RESP_TYPE_INT, "STATSTOTALDELETED"},
	{STATSTOTALUNKNOWN,   RESP_TYPE_INT, "STATSTOTALUNKNOWN"},

	{WRAPPER, RESP_TYPE_BLOBSTRING, "WRAPPER"},
	{COMMENT, RESP_TYPE_BLOBSTRING, "COMMENT"},
	{RESINUSE, RESP_TYPE_INT, "RESINUSE"},
	{SIGNAL, RESP_TYPE_INT, "SIGNAL"},

	{TAG_KEY,   RESP_TYPE_BLOBSTRING, "TAG_KEY"},
	{TAG_VALUE, RESP_TYPE_BLOBSTRING, "TAG_VALUE"},
	{PENDREASON, RESP_TYPE_INT, "PENDREASON"},

	{ENDOFFIELDS, RESP_TYPE_INT, "ENDOFFIELDS"}
};

static inline void setField(unsigned char * bitmap, int field_no) {
	bitmap[(field_no / 8)] |= 1 << (field_no % 8);
}

int isFieldSet(unsigned char * bitmap, int field_no) {
	return (bitmap[(field_no / 8)] >> (field_no % 8)) & 1U;
}

void freeStringArray(int count, char *** array) {
	int i;
	for (i = 0; i < count; i++)
		free((*array)[i]);

	free(*array);
	*array = NULL;

	return;
}

void freeStringMap(int count, key_val_t ** keys) {
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

	if (f->type != RESP_TYPE_INT)
		return -1;

	f->value.number = value;

	return 1;
}

const char * getFieldName(int field_no) {
	if (field_no >= ENDOFFIELDS)
		return NULL;

	return fields[field_no].name;
}

static int load_fields(msg_t * msg) {
	int64_t item_count = 0, map_count = 0;
	int64_t i, item;
	int rc = 0;
	int type;

	type = respGetType(&msg->reader);

	if (type == RESP_TYPE_ARRAY) {
		if (respGetArray(&msg->reader, &item_count)) {
			fprintf(stderr, "Failed to load fields - didn't get expected array item\n");
			return -1;
		}
	} else if (type == RESP_TYPE_MAP) {
		item_count = 1;
	} else {
		fprintf(stderr, "Unexpected type while loading request\n");
		return -1;
	}

	msg->item_count = item_count;
	msg->items = malloc(sizeof(msg_item) * item_count);

	for (item = 0; item < item_count; item++) {

		if (respGetMap(&msg->reader, &map_count)) {
			return -1;
		}

		msg->items[item].field_count = map_count;
		msg->items[item].fields = malloc(sizeof(field) * msg->items[item].field_count);
		memset(msg->items[item].bitmap, 0, sizeof(msg->items[item].bitmap));

		/* Load each key value pair
		 * Keys are always simple strings, we look these up
		 * in a table to determine the field type, etc. */

		for (i = 0; i < msg->items[item].field_count; i++) {
			char * key;

			if (respGetSimpleString(&msg->reader, &key))
				return -1;

			msg->items[item].fields[i].number = fieldtonum(key);

			if (msg->items[item].fields[i].number < 0)
				return -1;

			setField(msg->items[item].bitmap, msg->items[item].fields[i].number);

			msg->items[item].fields[i].type = fields[msg->items[item].fields[i].number].type;
			msg->items[item].fields[i].name = fields[msg->items[item].fields[i].number].name;

			/* Load the value */
			switch (fields[msg->items[item].fields[i].number].type) {
				case RESP_TYPE_BLOBSTRING:
					rc = respGetBlobString(&msg->reader, &msg->items[item].fields[i].value.string, NULL);
					break;
				case RESP_TYPE_INT:
					rc = respGetNum(&msg->reader, &msg->items[item].fields[i].value.number);
					break;
				case RESP_TYPE_BOOL:
					rc = respGetBool(&msg->reader, &msg->items[item].fields[i].value.boolean);
					break;
				case RESP_TYPE_ARRAY:
					rc = respGetArray(&msg->reader, &msg->items[item].fields[i].value.string_array.count);

					if (rc)
						break;

					msg->items[item].fields[i].value.string_array.strings = malloc(sizeof(char *) * msg->items[item].fields[i].value.string_array.count);

					for (int j = 0; j < msg->items[item].fields[i].value.string_array.count; j++) {
						rc = respGetBlobString(&msg->reader, &msg->items[item].fields[i].value.string_array.strings[j], NULL);

						if (rc)
							break;
					}

					break;

				case RESP_TYPE_MAP:
					rc = respGetMap(&msg->reader, &msg->items[item].fields[i].value.map.count);

					if (rc)
						break;

					msg->items[item].fields[i].value.map.keys = malloc(sizeof(key_val_t) * msg->items[item].fields[i].value.map.count);

					for (int j = 0; j <msg->items[item].fields[i].value.map.count; j++) {
						if ((rc = respGetBlobString(&msg->reader, &msg->items[item].fields[i].value.map.keys[j].key, NULL)))
							break;

						if ((rc = respGetBlobString(&msg->reader, &msg->items[item].fields[i].value.map.keys[j].value, NULL)))
							break;
					}
					break;
			}

			if (rc)
				return -1;
		}
	}

	return msg->item_count;
}

void free_message(msg_t * msg, buff_t * buff) {
	int i,j;

	for (j = 0; j < msg->item_count; j++) {

		for (i = 0; i < msg->items[j].field_count; i++) {
			if (fields[msg->items[j].fields[i].number].type == RESP_TYPE_ARRAY) {
				free(msg->items[j].fields[i].value.string_array.strings);
			} else if (fields[msg->items[j].fields[i].number].type == RESP_TYPE_MAP) {
				free(msg->items[j].fields[i].value.map.keys);
			}
		}
		free(msg->items[j].fields);
		msg->items[j].field_count = 0;
		msg->items[j].fields = NULL;
	}

	/* If the buffer has enough free space at the start, move everything down */
	if (buff) {
		if (buffRemove(buff, msg->reader.pos, 0)) {
			msg->reader.pos = 0;
			respReadUpdate(&msg->reader, buff->data, buff->used);
		}
	}

	respReadReset(&msg->reader);
	free(msg->items);

	msg->items = NULL;
	msg->item_count = 0;
	msg->command = NULL;
	msg->version = 0;
	msg->error = NULL;

	if (buff)
		load_message(msg, buff);
}

/* Load our string into a message structure
 *   Returns:
 *   <0 on error
 *    1 If there isn't enough data to load a request
 *    0 If loaded succesfully */

int load_message(msg_t * msg, buff_t * buff) {
	msg->version = 0;
	msg->item_count = 0;
	msg->items = NULL;
	msg->error = NULL;
	msg->command = NULL;

	respReadUpdate(&msg->reader, buff->data, buff->used);

	if (buff->used == 0)
		return 1;

	if (msg->reader.pos == msg->reader.length)
		return 1;

	int rc = respReadLoad(&msg->reader);

	/* Not enough data to load this request */
	if (rc == RESP_INCOMP) {
		return 1;
	} else if (rc != RESP_OK) {
		respReadFree(&msg->reader);
		fprintf(stderr, "Failed to parse request\n");
		return -1;
	}

	/* If its just a simple string, its a simple message */
	int type = respGetType(&msg->reader);

	if (type == RESP_TYPE_SIMPLESTRING) {
		if (respGetSimpleString(&msg->reader, &msg->command)) {
			respReadFree(&msg->reader);
			return -1;
		}
	} else if (type == RESP_TYPE_SIMPLEERROR) {
		if (respGetSimpleError(&msg->reader, &msg->error)) {
			respReadFree(&msg->reader);
			return -1;
		}
	} else if (type == RESP_TYPE_ARRAY) {
		int64_t count;

		if (respGetArray(&msg->reader, &count)) {
			respReadFree(&msg->reader);
			return -1;
		}

		/* Get command & version number */

		if (respGetSimpleString(&msg->reader, &msg->command)) {
			respReadFree(&msg->reader);
			return -1;
		}

		if (respGetNum(&msg->reader, &msg->version)) {
			respReadFree(&msg->reader);
			return -1;
		}

		/* Next up should be an array of map types containing fields */
		if (load_fields(msg) < 0) {
			respReadFree(&msg->reader);
			return -1;
		}
	} else {
		/* No clue what they sent us */
		respReadFree(&msg->reader);
		return -1;
	}

	return 0;
}

/* Initalise a new message */

int initMessage(resp_t * r, const char * resp_name, int version) {
	if (respNew(r) != 0)
		return 1;

	respAddArray(r);
	respAddSimpleString(r, resp_name);
	respAddInt(r, version);

	return 0;
}

void addIntField(resp_t * r, int field_no, int64_t value) {
	respAddSimpleString(r, fields[field_no].name);
	respAddInt(r, value);
}

void addStringField(resp_t * r, int field_no, char * value) {
	respAddSimpleString(r, fields[field_no].name);
	respAddBlobString(r, value, value ? strlen(value) : 0);
}

void addStringMapField(resp_t * r, int field_no, int count, key_val_t * keys) {
	respAddSimpleString(r, fields[field_no].name);
	respAddMap(r);

	for (int i = 0; i < count; i++) {
		respAddBlobString(r, keys[i].key, strlen(keys[i].key));
		respAddString(r, keys[i].value);
	}

	respCloseMap(r);
}

void addBoolField(resp_t * r, int field_no, char value) {
	respAddSimpleString(r, fields[field_no].name);
	respAddBool(r, value);
}

void addStringArrayField(resp_t * r, int field_no, int count, char ** strings) {
	respAddSimpleString(r, fields[field_no].name);
	respAddStringArray(r, count, strings);
}
