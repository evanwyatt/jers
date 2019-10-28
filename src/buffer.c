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

#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include <buffer.h>

/* Routines to manage generic dynamic buffers */

int buffNew(buff_t * b, size_t initial_size) {
	b->size = initial_size ? initial_size : BUFF_DEFAULT_SIZE;
	b->used = 0;

	b->data = malloc(b->size);

	if (!b->data) {
		fprintf(stderr, "buffNew - Failed to alloc memory: %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

void buffFree(buff_t * b) {
	free(b->data);
	b->data = NULL;
	b->used = 0;
	b->size = 0;
}

/* Resize the buffer to be able to store at least an extra 'length' bytes */
int buffResize(buff_t * b, size_t length) {
	size_t new_size = b->size;

	if  (length == 0)
		length = BUFF_DEFAULT_SIZE;

	if (new_size >= b->used + length)
		return 0;

	if (new_size == 0)
		new_size = BUFF_DEFAULT_SIZE;

	while (new_size <= b->used + length) {
		new_size *= 2;
	}

	char * new_data = realloc(b->data, new_size);

	if (new_data == NULL) {
		fprintf(stderr, "buffResize - realloc failed: %s\n", strerror(errno));
		return 1;
	}

	b->data = new_data;
	b->size = new_size;

	return 0;
}

/* Free memory to make the buffer no smaller than min_size or the default size
 * The buffer can't be shrunk smaller than used size */

void buffShrink(buff_t * b, size_t min_size) {
	size_t size = b->used > BUFF_DEFAULT_SIZE ? b->used : BUFF_DEFAULT_SIZE;

	if (min_size > size)
		size = min_size;

	char * resized = realloc(b->data, size);

	if (resized == NULL) {
		fprintf(stderr, "Failed to shrink buffer: %s\n", strerror(errno));
		return;
	}

	b->data = resized;
	b->size = size;
}

/* Add 'new_data' to the buffer */
int buffAdd(buff_t * b, const char * new_data, size_t data_size) {
	buffResize(b, data_size);

	memcpy(b->data + b->used, new_data, data_size);
	b->used += data_size;

	return 0;
}

/* Remove the data at start of the buffer of length 'data_size' if
 * its over a threshold */
int buffRemove(buff_t * b, size_t data_size, int shrink) {
	if (data_size < BUFF_USED_THRESHOLD)
		return 0;

	memmove(b->data, b->data + data_size, b->used - data_size);
	b->used -= data_size;

	if (shrink) {
		buffShrink(b, 0);
	}

	return 1;
}


void buffClear(buff_t * b, size_t size) {
	b->used = 0;
	buffShrink(b, size);
}
