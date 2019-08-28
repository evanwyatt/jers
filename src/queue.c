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

/* Create, validate and add a queue
 * Return: 0 = Success
 *         1 = validation failure
 *         2 = already exists       */

int addQueue(struct queue * q, int def, int dirty) {

	HASH_ADD_STR(server.queueTable, name, q);

	if (def)
		setDefaultQueue(q);

	q->obj.type = JERS_OBJECT_QUEUE;
	updateObject(&q->obj, dirty);
	return 0;
}

void setDefaultQueue(struct queue *q) {
	server.defaultQueue = q;
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

struct queue * findQueue(char * name) {
	struct queue * q = NULL;
	HASH_FIND_STR(server.queueTable, name, q);
	return q;
}

/* Cleanup queues that are marked as deleted, returning the number of queues cleaned up
 * - Only cleanup queues until the max_clean threshold is reached. */

int cleanupQueues(uint32_t max_clean) {
	uint32_t cleaned_up = 0;
	struct queue *q, *tmp;

	if (max_clean == 0)
		max_clean = 10;

	HASH_ITER(hh, server.queueTable, q, tmp) {
		if (!(q->internal_state &JERS_FLAG_DELETED))
			continue;

		/* Don't clean up queues flagged dirty or as being flushed */
		if (q->obj.dirty || q->internal_state &JERS_FLAG_FLUSHING)
			continue;

		/* Got a queue to remove */
		print_msg(JERS_LOG_DEBUG, "Removing deleted queue: %s", q->name);

		stateDelQueue(q);
		HASH_DEL(server.queueTable, q);
		freeQueue(q);

		if (++cleaned_up >= max_clean)
			break;
	}

	return cleaned_up;
}