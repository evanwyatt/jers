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

	{TAGS,       RESP_TYPE_ARRAY,      "TAGS"},
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
	{RESNAME,    RESP_TYPE_BLOBSTRING, "RESNAME"},
	{RESCOUNT,   RESP_TYPE_INT,        "RESCOUNT"},
};

char * getStringField(field *f) {
	char * ret = strdup(f->value.string);
	f->value.string = NULL;
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

	*array = f->value.string_array.strings;

	for (i = 0; i < f->value.string_array.count; i++)
		(*array)[i] = strdup(f->value.string_array.strings[i]);

	f->value.string_array.strings = NULL;

	return f->value.string_array.count;
}

static int fieldtonum(const char * in) {
	int i;
	int num_fields = sizeof(fields)/sizeof(field);

	for (i = 0; i < num_fields; i++) {
		if (strcmp(in, fields[i].name) == 0) return i;
	}

	return -1;
}

int load_fields(msg_t * msg) {
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

			msg->items[item].fields[i].type = fields[msg->items[item].fields[i].number].type;

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

					int j;
					for (j = 0; j < msg->items[item].fields[i].value.string_array.count; j++) {
						rc = respGetBlobString(&msg->reader, &msg->items[item].fields[i].value.string_array.strings[j], NULL);

						if (rc)
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
	msg->item_count = 0;
	msg->command = NULL;
	msg->version = 0;

	if (buff)
		load_message(msg, buff);
}

/* Load our string into a message structure
 *  * Returns:
 *   * <0 on error
 *    * 1 If there isn't enough data to load a request
 *     * 0 If loaded succesfully */

int load_message(msg_t * msg, buff_t * buff) {
	msg->version = 0;
	msg->item_count = 0;
	msg->items = NULL;

	respReadUpdate(&msg->reader, buff->data, buff->used);

	if (buff->used == 0)
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

void addIntField(resp_t * r, int field_no, int64_t value) {
	respAddSimpleString(r, fields[field_no].name);
	respAddInt(r, value);
}

void addStringField(resp_t * r, int field_no, char * value) {
	respAddSimpleString(r, fields[field_no].name);
	respAddBlobString(r, value, value ? strlen(value) : 0);
}

void addBoolField(resp_t * r, int field_no, char value) {
	respAddSimpleString(r, fields[field_no].name);
	respAddBool(r, value);
}

void addStringArrayField(resp_t * r, int field_no, int count, char ** strings) {
	respAddSimpleString(r, fields[field_no].name);
	respAddStringArray(r, count, strings);
}
