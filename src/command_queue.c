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
#include <agent.h>
#include <json.h>

void * deserialize_add_queue(msg_t * msg) {
	jersQueueAdd *q = calloc(sizeof(jersQueueAdd), 1);
	msg_item * item = &msg->items[0];
	int i;

	q->priority = JERS_QUEUE_DEFAULT_PRIORITY;
	q->state = 0;
	q->job_limit = 1;
	q->default_queue = -1;

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
	q->default_queue = -1;

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
	int default_queue = 0;
	int localhost = 0;

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

	if (qa->node == NULL) {
		qa->node = strdup("localhost");
		localhost = 1;
	} else if (strcasecmp(qa->node, "localhost") == 0) {
		localhost = 1;
	}

	/* Check the node provided is known to us */
	agent * a = agentList;

	while (a) {
		if (localhost) {
			if (strcasecmp(a->host, gethost()) == 0)
				break;
		} else if (strcasecmp(a->host, qa->node) == 0) {
			break;
		}
		a = a->next;
	}

	if (a == NULL) {
		free(q);
		sendError(c, JERS_ERR_INVARG, "Invalid hostname provided");
		return 1;
	}

	q->name = qa->name;
	q->host = qa->node;
	q->desc = qa->desc;
	q->job_limit = qa->job_limit != -1 ? qa->job_limit : JERS_QUEUE_DEFAULT_LIMIT;
	q->priority = qa->priority != -1 ? qa->priority : JERS_QUEUE_DEFAULT_PRIORITY;
	q->state = qa->state != -1 ? qa->state : JERS_QUEUE_DEFAULT_STATE;
	q->agent = a;
	default_queue = qa->default_queue != -1 ? qa->default_queue : 0;

	addQueue(q, default_queue, 1);

	return sendClientReturnCode(c, &q->obj, "0");
}

void serialize_jersQueue(buff_t *b, struct queue * q) {
	JSONStartObject(b, NULL);

	JSONAddString(b, QUEUENAME, q->name);

	if (q->desc)
		JSONAddString(b, DESC, q->desc);

	JSONAddString(b, NODE, q->host);
	JSONAddInt(b, JOBLIMIT, q->job_limit);
	JSONAddInt(b, STATE, q->state);
	JSONAddInt(b, PRIORITY, q->priority);
	JSONAddBool(b, DEFAULT, (server.defaultQueue == q));

	JSONAddInt(b, STATSRUNNING, q->stats.running);
	JSONAddInt(b, STATSPENDING, q->stats.pending);
	JSONAddInt(b, STATSHOLDING, q->stats.holding);
	JSONAddInt(b, STATSDEFERRED, q->stats.deferred);
	JSONAddInt(b, STATSCOMPLETED, q->stats.completed);
	JSONAddInt(b, STATSEXITED, q->stats.exited);

	JSONEndObject(b);
}

int command_get_queue(client *c, void *args) {
	jersQueueFilter * qf = args;
	struct queue * q = NULL;
	int all = 0;
	int64_t count = 0;

	buff_t b;

	if (qf->filters.name != NULL && strchr(qf->filters.name, '?') == NULL && strchr(qf->filters.name, '*') == NULL) {
		q = findQueue(qf->filters.name);

		if (q == NULL || q->internal_state &JERS_FLAG_DELETED) {
			sendError(c, JERS_ERR_NOQUEUE, NULL);
			return 1;
		}

		initResponse(&b, 1);
		serialize_jersQueue(&b, q);
		return sendClientMessage(c, NULL, &b);
	}

	initResponse(&b, 1);

	if (qf->filters.name == NULL || strcmp(qf->filters.name, "*") == 0)
		all = 1;

	for (q = server.queueTable; q != NULL; q = q->hh.next) {
		if ((!all && matches(qf->filters.name, q->name) != 0) || q->internal_state &JERS_FLAG_DELETED)
			continue;

		/* Made it here, add it to our response */
		serialize_jersQueue(&b, q);
		count++;
	}

	return sendClientMessage(c, NULL, &b);
}

int command_mod_queue(client *c, void * args) {
	jersQueueMod * qm = args;
	struct queue * q = NULL;
	int dirty = 0;
	agent *a = NULL;

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
		if (q->obj.revision >= server.recovery.revision) {
			print_msg(JERS_LOG_DEBUG, "Skipping recovery of queue_mod queue %s rev:%ld trans rev:%ld", q->name, q->obj.revision, server.recovery.revision);
			return 0;
		}
	}

	if (qm->node) {
		/* Check the node provided is known to us */
		a = agentList;
		int localhost = strcasecmp(qm->node, "localhost") == 0;

		while (a) {
			if (localhost) {
				if (strcasecmp(a->host, gethost()) == 0)
					break;
			} else if (strcasecmp(a->host, qm->node) == 0) {
				break;
			}
			a = a->next;
		}

		if (a == NULL) {
			sendError(c, JERS_ERR_INVARG, "Invalid hostname provided");
			return 1;
		}
	}

	if (qm->desc) {
		free(q->desc);
		q->desc = qm->desc;
		dirty = 1;
	}

	if (qm->node) {
		free(q->host);
		q->host = qm->node;
		q->agent = a;
		dirty = 1;
	}

	if (qm->state != -1 && q->state != qm->state) {
		q->state = qm->state;
		dirty = 1;
	}

	if (qm->job_limit != -1 && q->job_limit != qm->job_limit) {
		q->job_limit = qm->job_limit;
		dirty = 1;
	}

	if (qm->priority != -1 && q->priority != qm->priority) {
		q->priority = qm->priority;
		dirty = 1;
	}

	if (qm->default_queue != -1) {
		/* Clear the existing default queue and set this one */
		setDefaultQueue(q);
		dirty = 1;
	}

	updateObject(&q->obj, dirty);

	return sendClientReturnCode(c, &q->obj, "0");
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
		if (unlikely(server.recovery.in_progress)) {
			print_msg(JERS_LOG_DEBUG, "Skipping deletion of non-existent queue %s", qd->name);
			return 0;
		}

		sendError(c, JERS_ERR_NOQUEUE, NULL);
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

	return sendClientReturnCode(c, NULL, "0");
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
	UNUSED(status);
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
	UNUSED(status);
	free(qd->name);
	free(qd);
}
