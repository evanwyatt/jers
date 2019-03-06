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

#ifndef __RESP_H
#define __RESP_H

#include <stdint.h>
#include <sys/types.h>

struct buffer {
	char * string;
	size_t len;
	size_t size;
	size_t count;
};

typedef struct _resp_t {
	int64_t depth;
	int64_t size;
	struct buffer * items;
} resp_t;

typedef struct _resp_read_t {
	char * string;
	off_t length;
	off_t pos;		// Current parsing postion
	int ready;

	off_t start_pos;
	char * msg_cpy;

	/* Pointer to the current item being processed */
	struct argItem * currentItem;

	/* Linked list of data items allocated.
	 * This list is simply used to track allocated items. The structure of a
	 * message is tracked by the 'data' field. */
	struct argItem * args;
} resp_read_t;

struct argItem {
	int type;
	struct argItem * next;
		/* Note - The NULL type does not have an entry in the union */
	union {
		/* string is used to store BlobString/SimpleString/SimpleError */
		struct string {
			int64_t length;
			off_t offset; // The offset into to the buffer
		} string;

		struct number {
			int64_t value;
		} number;

		struct boolean {
			char value;
		} boolean;

		struct array {
			int64_t argc;
			int64_t loaded;
			struct argItem * items;
		} array;
	} data;
};

#define RESP_OK 0          // Everything is ok
#define RESP_ERR 1         // Generic ERROR
#define RESP_FORMATERR 2   // Encountered a formatting error decoding a stream
#define RESP_INCOMP 3      // The stream is incomplete, try again after reading more data
#define RESP_DONE 4        // All items have been read

enum resp_types {
	RESP_TYPE_BOOL = 1,
	RESP_TYPE_SIMPLESTRING,
	RESP_TYPE_SIMPLEERROR,
	RESP_TYPE_BLOBSTRING,
	RESP_TYPE_NULL,
	RESP_TYPE_INT,
	RESP_TYPE_ARRAY,
	RESP_TYPE_MAP,

	RESP_TYPE_NONE = 100
};

int respNew(resp_t * r);
char * respFinish(resp_t * r, size_t * len);

int respAddArray(resp_t * r);
int respCloseArray(resp_t * r);
int respAddMap(resp_t * r);
int respCloseMap(resp_t *r);

int respAddBool(resp_t * r, int boolean);  
int respAddSimpleError(resp_t * r, const char * error);
int respAddSimpleString(resp_t * r, const char * string);
int respAddStringArray(resp_t * r, int count, char ** strings);
int respAddString(resp_t * r, const char * string);
int respAddBlobString(resp_t * r, const char * string, uint64_t len);
int respAddNull(resp_t * r);
int respAddInt(resp_t * r, int64_t value);

void respReadInit(resp_read_t * r, char * input, size_t length);
int respReadReset(resp_read_t * r);
int respReadUpdate(resp_read_t * r, char * input, size_t new_length);
void respReadShrink(resp_read_t *r);
int respReadFree(resp_read_t * r);

int respReadLoad(resp_read_t * r);
int respGetType(resp_read_t * r);
int respGetArray(resp_read_t * r, int64_t * count);
int respGetMap(resp_read_t * r, int64_t * count);
int respGetNum(resp_read_t * r, int64_t * num);
int respGetBool(resp_read_t * r, char * bool);
int respGetBlobString(resp_read_t * r, char ** string, int64_t * length);
int respGetSimpleString(resp_read_t * r, char ** string);
int respGetSimpleError(resp_read_t * r, char ** string);
int respGetStringArray(resp_read_t * r, int64_t * count, char *** values);
#endif
