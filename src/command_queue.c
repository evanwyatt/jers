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
#include <error.h>

void * deserialize_add_queue(msg_t * msg) {
	jersQueueAdd *q = calloc(sizeof(jersQueueAdd), 1);
	msg_item * item = &msg->items[0];
	int i;

	q->priority = JERS_QUEUE_DEFAULT_PRIORITY;
	q->state = 0;
	q->job_limit = 1;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case QUEUENAME: q->name = getStringField(&item->fields[i]); break;
			case DESC: q->desc = getStringField(&item->fields[i]); break;
			case NODE: q->node = getStringField(&item->fields[i]); break;
			case PRIORITY: q->priority = getNumberField(&item->fields[i]); break;
			case JOBLIMIT: q->job_limit = getNumberField(&item->fields[i]); break;
			case STATE: q->state = getNumberField(&item->fields[i]); break;
			case DEFAULT: q->default_queue = getBoolField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",msg->items[0].fields[i].name); break;
		}
	}

	return q;
}

void * deserialize_get_queue(msg_t * msg) {
	jersQueueFilter *qf = calloc(sizeof(jersQueueFilter), 1);
	msg_item * item = &msg->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case QUEUENAME: qf->filters.name = getStringField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",msg->items[0].fields[i].name); break;
		}
	}

	return qf;
}

void * deserialize_mod_queue(msg_t * msg) {
	jersQueueMod *q = calloc(sizeof(jersQueueMod), 1);
	msg_item * item = &msg->items[0];
	int i;

	q->priority = -1;
	q->state = -1;
	q->job_limit = -1;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case QUEUENAME: q->name = getStringField(&item->fields[i]); break;
			case DESC: q->desc = getStringField(&item->fields[i]); break;
			case NODE: q->node = getStringField(&item->fields[i]); break;
			case PRIORITY: q->priority = getNumberField(&item->fields[i]); break;
			case JOBLIMIT: q->job_limit = getNumberField(&item->fields[i]); break;
			case STATE: q->state = getNumberField(&item->fields[i]); break;
			case DEFAULT: q->default_queue = getBoolField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",msg->items[0].fields[i].name); break;
		}
	}

	return q;
}

void * deserialize_del_queue(msg_t * msg) {
	jersQueueDel *q = calloc(sizeof(jersQueueDel), 1);
	msg_item * item = &msg->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case QUEUENAME: q->name = getStringField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",msg->items[0].fields[i].name); break;
		}
	}

	return q;
}

int command_add_queue(client * c, void * args) {
	jersQueueAdd * qa = args;
	struct queue * q = NULL;

	lowercasestring(qa->name);

	if (check_name(qa->name)) {
		sendError(c, JERS_ERR_INVARG, "Invalid queue name");
		return 1;
	}

	q = findQueue(qa->name);

	if (q != NULL) {
		if (server.recovery.in_progress)
			return 0;

		if ((q->internal_state &JERS_FLAG_DELETED) == 0) {
			sendError(c, JERS_ERR_QUEUEEXISTS, NULL);
			return 1;
		} else {
			/* Asked to create a queue which has just been deleted.
			 * It's easier to remove it from the hashtable, then re-add it. */
			HASH_DEL(server.queueTable, q);

			free(q->name);
			free(q->desc);
			free(q->host);
			q->internal_state &= ~JERS_FLAG_DELETED;
		}
	} else {
		q = calloc(sizeof(struct queue), 1);
	}

	q->name = qa->name;
	q->host = qa->node;
	q->desc = qa->desc;
	q->job_limit = qa->job_limit != -1 ? qa->job_limit : JERS_QUEUE_DEFAULT_LIMIT;
	q->priority = qa->priority != -1 ? qa->priority : JERS_QUEUE_DEFAULT_PRIORITY;
	q->state = qa->state != -1 ? qa->state : JERS_QUEUE_DEFAULT_STATE;
	q->agent = NULL;
	q->revision = 1;

	addQueue(q, qa->default_queue, 1);

	/* Check if we need to link this queue to an agent that
	 * is already connected . */

	agent * a = server.agent_list;

	while (a) {
		if (strcmp(a->host, q->host) == 0) {
			q->agent = a;
			break;
		} else if (strcmp(q->host, "localhost") == 0) {
			if (strcmp(a->host, gethost()) == 0) {
				q->agent = a;
				break;
			}
		}
	}

	return sendClientReturnCode(c, "0");
}

void serialize_jersQueue(resp_t * r, struct queue * q) {
	respAddMap(r);

	addStringField(r, QUEUENAME, q->name);
	addStringField(r, DESC, q->desc);
	addStringField(r, NODE, q->host);
	addIntField(r, JOBLIMIT, q->job_limit);
	addIntField(r, STATE, q->state);
	addIntField(r, PRIORITY, q->priority);
	addBoolField(r, DEFAULT, (server.defaultQueue == q));

	addIntField(r, STATSRUNNING, q->stats.running);
	addIntField(r, STATSPENDING, q->stats.pending);
	addIntField(r, STATSHOLDING, q->stats.holding);
	addIntField(r, STATSDEFERRED, q->stats.deferred);
	addIntField(r, STATSCOMPLETED, q->stats.completed);
	addIntField(r, STATSEXITED, q->stats.exited);

	respCloseMap(r);
}

int command_get_queue(client *c, void *args) {
	jersQueueFilter * qf = args;
	struct queue * q = NULL;
	int all = 0;
	int64_t count = 0;

	resp_t r;

	if (qf->filters.name != NULL && strchr(qf->filters.name, '?') == NULL && strchr(qf->filters.name, '*') == NULL) {
		q = findQueue(qf->filters.name);

		if (q == NULL || q->internal_state &JERS_FLAG_DELETED) {
			sendError(c, JERS_ERR_NOQUEUE, NULL);
			return 1;
		}

		initMessage(&r, "RESP", 1);
		respAddArray(&r);
		serialize_jersQueue(&r, q);
		respCloseArray(&r);
		return sendClientMessage(c, &r);
	}

	initMessage(&r, "RESP", 1);

	respAddArray(&r);

	if (qf->filters.name == NULL || strcmp(qf->filters.name, "*") == 0)
		all = 1;

	for (q = server.queueTable; q != NULL; q = q->hh.next) {
		if ((!all && matches(qf->filters.name, q->name) != 0) || q->internal_state &JERS_FLAG_DELETED)
			continue;

		/* Made it here, add it to our response */
		serialize_jersQueue(&r, q);
		count++;
	}

	respCloseArray(&r);

	return sendClientMessage(c, &r);
}

int command_mod_queue(client *c, void * args) {
	jersQueueMod * qm = args;
	struct queue * q = NULL;

	if (qm->name == NULL) {
		sendError(c, JERS_ERR_INVARG, "No queue provided");
		return 1;
	}

	q = findQueue(qm->name);

	if (q == NULL) {
		sendError(c, JERS_ERR_NOQUEUE, NULL);
		return 1;
	}

	if (unlikely(server.recovery.in_progress)) {
		if (q->revision >= server.recovery.revision) {
			print_msg(JERS_LOG_DEBUG, "Skipping recovery of queue_mod queue %s rev:%ld trans rev:%ld", q->name, q->revision, server.recovery.revision);
			return 0;
		}
	}

	if (qm->desc && (q->desc == NULL || strcmp(q->desc, qm->desc) == 0)) {
		free(q->desc);
		q->desc = qm->desc;
		q->dirty = 1;
	}

	if (qm->node) {
		free(q->host);
		q->host = qm->node;
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

	server.dirty_queues = 1;
	q->revision++;

	return sendClientReturnCode(c, "0");
}

int command_del_queue(client *c, void *args) {
	jersQueueDel *qd = args;
	struct queue *q = NULL;
	struct job *j = NULL;

	if (qd->name == NULL) {
		sendError(c, JERS_ERR_INVARG, "No queue provided");
		return 1;
	}

	q = findQueue(qd->name);

	if (q == NULL || q->internal_state &JERS_FLAG_DELETED) {
		sendError(c, JERS_ERR_NOQUEUE, NULL);
		return 1;
	}

	if (q == server.defaultQueue) {
		sendError(c, JERS_ERR_INVARG, "Can't delete the default queue");
		return 1;
	}

	/* We can only delete a queue if there are no active jobs on it. Deleted jobs are ok. */
	for (j = server.jobTable; j != NULL; j = j->hh.next) {
		if (j->queue == q && !(j->internal_state &JERS_FLAG_DELETED))
			break;
	}

	if (j != NULL) {
		sendError(c, JERS_ERR_NOTEMPTY, NULL);
		return 1;
	}

	q->internal_state |= JERS_FLAG_DELETED;

	return sendClientReturnCode(c, "0");
}

void free_add_queue(void * args, int status) {
	jersQueueAdd *q = args;

	if (status) {
		free(q->name);
		free(q->desc);
		free(q->node);
	}

	free(q);
}

void free_get_queue(void * args, int status) {
	jersQueueFilter *qf = args;

	free(qf->filters.name);
	free(qf);
}

void free_mod_queue(void * args, int status) {
	jersQueueMod * qm = args;
	
	if (status) {
		free(qm->desc);
		free(qm->node);
	}

	free(qm->name);
	free(qm);
}

void free_del_queue(void * args, int status) {
	jersQueueDel * qd = args;
	free(qd->name);
	free(qd);
}
