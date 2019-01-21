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
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include <server.h>
#include <jers.h>

int validateQueueName(char * name) {
	int i;
	int len;

	if (!name) {
		fprintf(stderr, "Queuename: Not passed.\n");
		return 1;
	}

	len = strlen(name);

	/* All characters must be printable */
	for (i = 0; i < len; i++) {
		if (!isprint(name[i])) {
			fprintf(stderr, "QueueName: %s contians an unprintable character\n", name);
			return 1;
		}
	}

	/* Invalid Characters */
	char * ptr = strpbrk(name, JERS_QUEUE_INVALID_CHARS);

	if (ptr) {
		fprintf(stderr, "QueueName: %s contains invalid characters for a queuename. Invalid='%s'\n", name, JERS_QUEUE_INVALID_CHARS);
		return 1;
	}

	return 0;
}

int validateQueuePriority (int priority) {
	if (priority < 0 || priority > JERS_QUEUE_MAX_PRIORITY) {
		fprintf(stderr, "Queue priority bad: %d\n", priority);
		return 1;
	}

	return 0;
}

int validateQueueDesc(char * desc) {
	if (!desc) {
		return 0;
	}

	return 0;
}

int validateQueueJobLimit(int limit) {
	if (limit < 0 || limit > JERS_QUEUE_MAX_LIMIT){
		return 1;
	}

	return 0;
}

/* Create, validate and add a queue
 * Return: 0 = Success
 *         1 = validation failure
 *         2 = already exists       */

int addQueue(struct queue * q, int def, int dirty) {
	struct queue * check = NULL;

	if (validateQueueName(q->name)) {
		return 1;
	}

	if (validateQueueDesc(q->desc)) {
		return 1;
	}

	if (validateQueuePriority(q->priority)) {
		return 1;
	}

	if (validateQueueJobLimit(q->job_limit)) {
		return 1;
	}

	lowercasestring(q->name);

	/* Check if it already exists */
	HASH_FIND_STR(server.queueTable, q->name, check);

	if (check != NULL) {
		print_msg(JERS_LOG_CRITICAL, "Queue %s already exists\n", q->name);
		/* Already exists */
		return 2;
	}

	q->state = JERS_QUEUE_DEFAULT_STATE;

	HASH_ADD_STR(server.queueTable, name, q);

	if (def || server.defaultQueue == NULL) {
		server.defaultQueue = q;
	}

	if (dirty) {
		q->dirty = 1;
		server.dirty_queues++;
	}

	return 0;
}

void freeQueue(struct queue * q) {
	free(q->name);
	free(q->desc);
	free(q->host);

	free(q);
}

void removeQueue(struct queue * q) {
	HASH_DEL(server.queueTable, q);
	freeQueue(q);
}
