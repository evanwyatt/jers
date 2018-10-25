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
#include <server.h>
#include <jers.h>
#include <commands.h>
#include <fields.h>

void * deserialize_add_queue(msg_t * msg) {
	jersQueueAdd *qa = calloc(sizeof(jersQueueAdd), 1);
	
	
	return qa;
}

void * deserialize_get_queue(msg_t * msg) {
	jersQueueFilter *q = calloc(sizeof(jersQueueFilter), 1);
	
	
	return q;
}

void * deserialize_mod_queue(msg_t * msg) {
	jersQueueMod *q = calloc(sizeof(jersQueueMod), 1);
	
	
	return q;
}

void * deserialize_del_queue(msg_t * msg) {
	jersQueueDel *q = calloc(sizeof(jersQueueDel), 1);
	
	
	return q;
}

int command_add_queue(client * c, void * args) {
	jersQueueAdd * qa = args;
	struct queue * q = NULL;
	resp_t * response = NULL;
	char * buff = NULL;
	size_t buff_length = 0;

	HASH_FIND_STR(server.queueTable, qa->name, q);

	if (q != NULL) {
		appendError(c, "-QUEUEEXISTS Queue already exists\n");
		return 1;
	}

	q = malloc(sizeof(struct queue));
	q->name = strdup(qa->name);
	q->host = strdup(qa->node);
	q->desc = qa->desc ? strdup(qa->desc) : NULL;
	q->job_limit = qa->job_limit != -1 ? qa->job_limit : JERS_QUEUE_DEFAULT_LIMIT;
	q->priority = qa->priority != -1 ? qa->priority : JERS_QUEUE_DEFAULT_PRIORITY;
	q->state = qa->state != -1 ? qa->state : JERS_QUEUE_DEFAULT_STATE;
	q->agent = NULL;
	q->dirty = 1;

	HASH_ADD_STR(server.queueTable, name, q);

	/* Check if we need to link this queue to an agent that
	 * is already connected . */

	struct agent * a = server.agent_list;

	while (a) {
		if (strcmp(a->host, q->host) == 0) {
			q->agent = a;
			break;
		}
		else if (strcmp(q->host, "localhost") == 0) {
			if (strcmp(a->host, gethost()) == 0) {
				q->agent = a;
				break;
			}	
		}
	}

	response = respNew();
	respAddSimpleString(response, "0");
	buff = respFinish(response, &buff_length);
	appendResponse(c, buff, buff_length);
	free(buff);

	return 0;
}

int command_get_queue(client *c, void *args) {
	jersQueueFilter * qf = args;
	
	
	return 0;
}

int command_mod_queue(client *c, void * args) {
	jersQueueMod * qm = args;
	struct queue * q = NULL;
	resp_t * response = NULL;
	char * buff = NULL;
	size_t buff_length = 0;

	HASH_FIND_STR(server.queueTable, qm->name, q);

	if (q == NULL) {
		fprintf(stderr, "Didn't find queue %s\n", qm->name);
		appendResponse(c, "-NOQUEUE Queue does not exists\n", 31);
		return 1;
	}

	if (qm->desc && (q->desc == NULL || strcmp(q->desc, qm->desc) == 0)) {
		free(q->desc);
		q->desc = strdup(qm->desc);
		q->dirty = 1;
	}

	if (qm->state != -1 && q->state != qm->state) {
		q->state = qm->state;
		q->dirty = 1;
	}

	if (qm->job_limit != -1 && q->job_limit != qm->job_limit) {
		q->job_limit = qm->job_limit;
		q->dirty = 1;
	}

	if (qm->priority != -1 && q->priority != qm->priority) {
		q->priority = qm->priority;
		q->dirty = 1;
	}

	response = respNew();
	respAddSimpleString(response, "0");
	buff = respFinish(response, &buff_length);
	appendResponse(c, buff, buff_length);
	free(buff);

	return 0;
}

int command_del_queue(client *c, void *args) {
	jersQueueDel *qd = args;
	
	
	return 0;
}

