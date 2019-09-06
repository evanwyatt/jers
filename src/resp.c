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
#include <inttypes.h>

#include "resp.h"
#include "common.h"

#define INIT_SIZE 1024
#define DELIM_CHAR '\n'

/* A simple implementation of the RESP3 protocol for serialization and deserialization
 * - Not all data type are implemented */

static inline int _resize(struct buffer * b, int needed) {
	if (b->size > b->len + needed)
		return 0;

	if (b->size == 0)
		b->size = INIT_SIZE;

	while(b->size <= b->len + needed)
		b->size *= 2;

	char * new = realloc(b->string, b->size);
	b->string = new;

	return 0;
}

static inline char * _respGetBytes(resp_read_t * r, int wanted) {
	char * ret = NULL;

	/* Return a valid pointer if we have enough bytes,
 	 * and advance our position in the stream */
	if (r->length > r->pos + wanted) {
		ret = r->string + r->pos;
		r->pos += wanted;
	}

	return ret;
}

/* Keep consuming bytes until we hit the delimiters
 * - If we don't hit a delimiter before we run out of bytes,
 *   return NULL to signify we need more data */

static inline char * _respGetLine(resp_read_t * r) {
	char * pos = r->string + r->pos;

	if (r->length <= r->pos)
		return NULL;

	while (pos < r->string + r->length && *pos != DELIM_CHAR) {
		pos++;
	}

	if (pos >= r->string + r->length)
		return NULL;

	if (*pos == DELIM_CHAR) {
		char * ret = r->string + r->pos;
		r->pos = (pos - r->string) + 1; // +1 to consume the delimiter
		return ret;
	}

	return NULL;
}

static inline int _respConvertNum(char * str, int64_t * result) {
	/* Given str, read until the delimter is hit, converting the
 	 * the number to an int64_t */
	*result = 0;
	char neg = 0;

	if (*str == '-') {
		neg = 1;
		str++;
	}

	while (*str != DELIM_CHAR) {
		*result = (*result * 10) + *str - '0';
		str++;
	}

	if (neg)
		*result = -(*result);

	return RESP_OK;
}

static inline void _respFreeItem(struct argItem * item) {
	if (item->type == RESP_TYPE_ARRAY || item->type == RESP_TYPE_MAP) {
		while (item->data.array.loaded) {
			_respFreeItem(&item->data.array.items[--item->data.array.loaded]);
		}
		free(item->data.array.items);
	}

	return;
}

static int _respGetItem(resp_read_t * r, struct argItem * item) {
	char * str = NULL;

	/* This function can be called multiple times to try and populate
	 * the same item, as we might have already consumed and populated the type */

	if (item->type == 0) {
		if ((str = _respGetLine(r)) == NULL)
			return RESP_INCOMP;

		switch (*str) {
			case '*': item->type = RESP_TYPE_ARRAY; break;
			case '$': item->type = RESP_TYPE_BLOBSTRING; break;
			case ':': item->type = RESP_TYPE_INT; break;
			case '#': item->type = RESP_TYPE_BOOL; break;
			case '_': item->type = RESP_TYPE_NULL; break;
			case '-': item->type = RESP_TYPE_SIMPLEERROR; break;
			case '+': item->type = RESP_TYPE_SIMPLESTRING; break;
			case '%': item->type = RESP_TYPE_MAP; break;
			default: return RESP_ERR;
		}

		// Advance past the type
		str++;

		// Chain the items together
		if (r->currentItem)
			r->currentItem->next = item;

		r->currentItem = item;

		switch (item->type) {

			case RESP_TYPE_NULL:
				/* A NULL type has no data to populate */
				return RESP_OK;

			case RESP_TYPE_BOOL:
				item->data.boolean.value = *str == 't' ? 1:0;
				return RESP_OK;

			case RESP_TYPE_SIMPLESTRING:
			case RESP_TYPE_SIMPLEERROR:
				item->data.string.offset = str - r->string;
				item->data.string.length = (r->string + r->pos) - str - 1;
				//*(str + item->data.string.length) = '\0'; // NULL terminate it
				//r->pos += item->data.string.length;
				return RESP_OK;

			case RESP_TYPE_INT:
				_respConvertNum(str, &item->data.number.value);
				return RESP_OK;


			case RESP_TYPE_BLOBSTRING:
				_respConvertNum(str, &item->data.string.length);
				break;

			case RESP_TYPE_ARRAY:
			case RESP_TYPE_MAP:
				_respConvertNum(str, &item->data.array.argc);
				break;
		}
	}

	if (item->type == RESP_TYPE_BLOBSTRING) {
		str = _respGetBytes(r, item->data.string.length);
		if (str == NULL)
			return RESP_INCOMP;

		item->data.string.offset = str - r->string;
		//*(str + item->data.string.length) = '\0';
		r->pos++; // Skip the delimiter
		return RESP_OK;
	}

	/* Maps/Array types left now
	 * Maps are treated as arrays for the most part */
	 int64_t count = item->type == RESP_TYPE_ARRAY ? item->data.array.argc : (item->data.array.argc * 2);
	/* Allocate enough room for them */
	if (item->data.array.items == NULL) {
		item->data.array.items = calloc(sizeof(struct argItem), count);
	}

	int rc;
	while (item->data.array.loaded < count) {
		if ((rc = _respGetItem(r, &item->data.array.items[item->data.array.loaded])) != RESP_OK) {
			if (rc == RESP_INCOMP)
			return rc;
		}
		item->data.array.loaded++;
	}

	return RESP_OK;
}


int respNew(resp_t * r) {
	r->items = malloc(sizeof(struct buffer) * INIT_SIZE);

	if (r->items == NULL)
		return 1;

	r->size = INIT_SIZE;
	r->depth = 0;

	r->items->len = 0;
	r->items->count = 0;
	r->items->string = NULL;
	r->items->size = 0;

	return 0;
}

/* Return the completed buffer, and the length.
 * Free any memory we allocated as part of this processing */

char * respFinish(resp_t * r, size_t * len) {
	char * buffer = r->items[0].string;

	if (len)
		*len  = r->items[0].len;

	free(r->items);

	return buffer;
}

/* */

int respAddArray(resp_t * r) {
	if (r->depth + 1 >= INT64_MAX) {
		fprintf(stderr, "Maximum array depth reached\n");
		return 1;
	}

	r->items[r->depth].count++;
	r->depth++;

	if (r->size < r->depth + 1) {
		r->size *= 2;
		struct buffer * new_buffer = realloc(r->items, r->size);

		r->items = new_buffer;
	}

	r->items[r->depth].string = malloc(INIT_SIZE);
	r->items[r->depth].size = INIT_SIZE;
	r->items[r->depth].len = 0;
	r->items[r->depth].count = 0;

	return 0;
}

int respAddMap(resp_t * r) {
	return respAddArray(r);
}

static int _respClose(resp_t * r, int type) {
	int64_t count;

	/* Add the current 'depth' item to previous one, with the count at the start */
	if (r->depth == 0) {
		fprintf(stderr, "Can't be in an array\n");
		return 1;
	}

	r->depth--;
	struct buffer * b = &r->items[r->depth];

	_resize(b, r->items[r->depth + 1].len + 32);

	count = (type == RESP_TYPE_ARRAY)? r->items[r->depth + 1].count : (r->items[r->depth + 1].count / 2);

	b->string[b->len++] = type == RESP_TYPE_ARRAY? '*':'%';
	b->len += int64tostr(b->string + b->len, count);
	b->string[b->len++] = DELIM_CHAR;

	memcpy(b->string + b->len, r->items[r->depth + 1].string, r->items[r->depth + 1].len);
	b->len += r->items[r->depth + 1].len;
	b->string[b->len] = 0;

	free(r->items[r->depth + 1].string);
	r->items[r->depth + 1].string = NULL;
	r->items[r->depth + 1].size = 0;
	return 0;
}

int respCloseArray(resp_t * r) {
	return _respClose(r, RESP_TYPE_ARRAY);
}

int respCloseMap(resp_t * r) {
	return _respClose(r, RESP_TYPE_MAP);
}

int respAddInt(resp_t * r, int64_t value) {
	struct buffer * b = &r->items[r->depth];

	_resize(b, 32);
	b->string[b->len++] = ':';
	b->len += int64tostr(b->string + b->len, value);
	b->string[b->len++] = DELIM_CHAR;
	b->string[b->len] = '\0';
	b->count++;

	return 0;
}

int respAddNull(resp_t * r) {
	struct buffer * b = &r->items[r->depth];
	_resize(b, 6);

	b->string[b->len++] = '_';
	b->string[b->len++] = DELIM_CHAR;
	b->string[b->len] = '\0';
	b->count++;

	return 0;
}

int respAddBlobString(resp_t * r, const char * string, uint64_t len) {
	struct buffer * b = &r->items[r->depth];
	_resize(b, len + 32);

	if (string == NULL)
		return respAddNull(r);

	b->string[b->len++] = '$';
	b->len += int64tostr(b->string + b->len, len);
	b->string[b->len++] = DELIM_CHAR;
	memcpy(b->string + b->len, string, len);
	b->len += len;
	b->string[b->len++] = DELIM_CHAR;
	b->string[b->len] = '\0';
	b->count++;
	return 0;
}

int respAddString(resp_t * r, const char * string) {
	if (string)
		return respAddBlobString(r, string, strlen(string));
	else
		return respAddNull(r);
}

int respAddStringArray(resp_t * r, int count, char ** strings) {
	int i;

	respAddArray(r);

	for(i = 0; i < count; i++) {
		respAddBlobString(r, strings[i], strlen(strings[i]));
	}

	respCloseArray(r);

	return 0;
}

int respAddSimpleString(resp_t * r, const char * string) {
	struct buffer * b = &r->items[r->depth];
	int len = strlen(string);
	_resize(b, len + 8);

	b->string[b->len++] = '+';
	memcpy(b->string + b->len, string, len + 1);
	b->len += len;
	b->string[b->len++] = DELIM_CHAR;
	b->count++;

	return 0;
}

int respAddSimpleError(resp_t * r, const char * error) {
	struct buffer * b = &r->items[r->depth];
	int len;
	_resize(b, strlen(error) + 8);

	len = strlen(error);

	b->string[b->len++] = '-';
	strcpy(b->string + b->len, error);
	b->len += len;
	b->string[b->len++] = DELIM_CHAR;
	b->count++;

	return 0;
}

int respAddBool(resp_t * r, int boolean) {
	struct buffer * b = &r->items[r->depth];
	_resize(b, 3);

	b->string[b->len++] = '#';
	b->string[b->len++] = boolean ? 't':'f';
	b->string[b->len++] = DELIM_CHAR;
	b->string[b->len] = '\0';
	b->count++;

	return 0;
}

/****** Reader Routines *******/

void respReadInit (resp_read_t * r, char * input, size_t length){
	r->string = input;
	r->length = length;
	r->pos = 0;
	r->start_pos = 0;
	r->ready = 0;
	r->currentItem = NULL;
	r->args = NULL;

	r->msg_cpy = NULL;
}

/* Update the buffer/length already initalised by respReadInit */

int respReadUpdate(resp_read_t * r, char * input, size_t new_length) {
	r->string = input;
	r->length = new_length;
	return RESP_OK;
}

/* Free all memory associated with the resp_read_t struct, including all the arg structures
 * The original buffer loaded by respReadInit is not touched. */

int respReadFree(resp_read_t * r) {
	if (r->args) {
		_respFreeItem(r->args);
		free(r->args);
		r->args = NULL;
	}

	free(r->msg_cpy);
	r->msg_cpy = NULL;

	r->currentItem = NULL;
	r->string = NULL;
	r->pos = 0;
	r->length = 0;
	r->ready = 0;
	r->start_pos = 0;

	return RESP_OK;
}

int respReadLoad(resp_read_t * r) {
	int rc;

	if (r->args == NULL) {
		r->args = calloc(sizeof(struct argItem), 1);
	}

	if (r->start_pos == 0)
		r->start_pos = r->pos;

	rc = _respGetItem(r, r->args);

	if (rc == RESP_OK) {
		/* The request has been loaded, so point back to the first item,
		 * so we are ready for processing */
		r->currentItem = r->args;
		r->ready = 1;

		/* Create a copy of the original message. This will be written to the transaction journal.
		 * If we already have a message copy, we are replaying a command and don't need to save a copy */
		if (r->msg_cpy == NULL) {
			size_t length = r->pos - r->start_pos;
			r->msg_cpy = dup_mem(r->string + r->start_pos, length, length + 1);
			r->msg_cpy[length] = '\0';
		}
	}

	return rc;
}

/* Remove old request data if over a threshold
 *  This should only happen for agent request/response buffers
 *  and large pipelined requests
 *  Note: This can't be done while a request is being processed */

void respReadShrink(resp_read_t *r) {

	if (r->ready == 0)
		return;

	if (r->pos < 1024)
		return;

	memmove(r->string, r->string + r->pos, r->length - r->pos);
	r->length -= r->pos;
	r->pos = 0;
	r->start_pos = 0;
}

/* Clear the previous data loaded, ready for the next message */
int respReadReset(resp_read_t * r) {

	/* Free the previous request items first */
	if (r->args) {
		_respFreeItem(r->args);
		memset(r->args, 0, sizeof(struct argItem));
	}

	free(r->msg_cpy);
	r->msg_cpy = NULL;

	r->start_pos = 0;
	r->ready = 0;
	r->currentItem = NULL;

	return 0;
}

static void _respReadAdvance(resp_read_t * r) {
	r->currentItem = r->currentItem->next;
}

/* Helper functions to return specific types at the current depth */

/* Return the current type, or -1 on error */
int respGetType(resp_read_t * r) {
	if (!r->currentItem) {
		return RESP_TYPE_NONE;
	}

	return r->currentItem->type;
}

int respGetArray(resp_read_t * r, int64_t * count) {
	if (!r->currentItem) {
		return RESP_DONE;
	}

	if (r->currentItem->type != RESP_TYPE_ARRAY) {
		return RESP_ERR;
	}

	*count = r->currentItem->data.array.argc;
	_respReadAdvance(r);
	return RESP_OK;
}

int respGetMap(resp_read_t * r, int64_t * count) {
	if (!r->currentItem) {
		return RESP_DONE;
	}

	if (r->currentItem->type != RESP_TYPE_MAP) {
		return RESP_ERR;
	}

	*count = r->currentItem->data.array.argc;
	_respReadAdvance(r);
	return RESP_OK;
}

int respGetNum(resp_read_t * r, int64_t * num) {
	if (!r->currentItem) {
		return RESP_DONE;
	}

	if (r->currentItem->type != RESP_TYPE_INT) {
		fprintf(stderr, "Current type is not a NUMBER type: %d\n", r->currentItem->type);
		return RESP_ERR;
	}

	*num = r->currentItem->data.number.value;

	/* Move to the next item */
	_respReadAdvance(r);

	return RESP_OK;
}

int respGetBool(resp_read_t * r, char * bool) {
	if (!r->currentItem) {
		return RESP_DONE;
	}

	if (r->currentItem->type != RESP_TYPE_BOOL) {
		fprintf(stderr, "Current type is not a BOOL type: %d\n", r->currentItem->type);
		return RESP_ERR;
	}

	*bool = r->currentItem->data.boolean.value;

	/* Move to the next item */
	_respReadAdvance(r);

	return RESP_OK;
}

static int _respGetString(resp_read_t * r, int type, char ** string, int64_t * length) {
	if (!r->currentItem) {
		return RESP_DONE;
	}

	if (r->currentItem->type == RESP_TYPE_NULL) {
		*string = NULL;
		if (length)
			*length = 0;
	} else {
		if (r->currentItem->type != type) {
			fprintf(stderr, "Current type is not a %d type: %d\n", type, r->currentItem->type);
			return RESP_ERR;
		}

		*string = r->string + r->currentItem->data.string.offset;

		if (length)
			*length = r->currentItem->data.string.length;

		/* NULL terminate the string */
		*(*string + r->currentItem->data.string.length) = '\0';
	}
	/* Move to the next item */
	_respReadAdvance(r);

	return RESP_OK;
}

int respGetStringArray(resp_read_t * r, int64_t * count, char *** values) {
	int64_t i;
	char ** temp;
	respGetArray(r, count);

	if (*count == 0) {
		*values = NULL;
		return 0;
	}

	temp = malloc(sizeof(char *) * *count);

	for (i = 0; i < *count; i++) {
		_respGetString(r, RESP_TYPE_BLOBSTRING, &temp[i], NULL);
	}

	*values = temp;
	return 0;
}

int respGetBlobString(resp_read_t * r, char ** string, int64_t * length) {
	return _respGetString(r, RESP_TYPE_BLOBSTRING, string, length);
}

int respGetSimpleString(resp_read_t * r, char ** string) {
	return _respGetString(r, RESP_TYPE_SIMPLESTRING, string, NULL);
}

int respGetSimpleError(resp_read_t * r, char ** string) {
	return _respGetString(r, RESP_TYPE_SIMPLEERROR, string, NULL);
}
