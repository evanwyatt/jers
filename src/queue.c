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
#include <fnmatch.h>

#include <server.h>
#include <jers.h>

#include <json.h>

/* Create, validate and add a queue
 * Return: 0 = Success
 *         1 = validation failure
 *         2 = already exists       */

int addQueue(struct queue * q, int dirty) {

	HASH_ADD_STR(server.queueTable, name, q);

	if (q->def)
		setDefaultQueue(q);

	q->obj.type = JERS_OBJECT_QUEUE;
	updateObject(&q->obj, dirty);

	/* Work out the permissions for this queue based off ACLs */
	struct queue_acl *acl;

	LIST_ITER(&server.queue_acls, acl) {
		if (fnmatch(acl->expr, q->name, 0) == 0) {
			/* Expression matches this queue name. We need to add/remove
			 * the permissions for the gids */
			for (int i = 0; acl->gids[i]; i++) {
				struct gid_perm *gid = NULL;
				HASH_FIND_INT(q->permissions, &acl->gids[i], gid);

				if (gid == NULL) {
					/* GID not listed on this queue, add it if we are allowing permissions */
					if (!acl->allow)
						continue;

					gid = calloc(1, sizeof(struct gid_perm));
					gid->gid = acl->gids[i];

					HASH_ADD_INT(q->permissions, gid, gid);
				}

				if (acl->allow)
					gid->perm |= acl->permissions;
				else
					gid->perm &= ~acl->permissions;
			}
		}
	}

	return 0;
}

int checkQueueACL(client *c, struct queue *q, int required_perms) {
	int perms = 0;

	if (c->uid == 0)
		return 0;

	/* Are they a global queue admin? */
	if (c->user->permissions &PERM_QUEUE)
		return 0;

	/* For each group a user is a member of, check if the queue has permissions listed for it */
	for (int i = 0; i < c->user->group_count; i++) {
		struct gid_perm *gp = NULL;
		HASH_FIND_INT(q->permissions, &c->user->group_list[i], gp);

		if (gp) {
			perms |= gp->perm;

			if ((perms &required_perms) == required_perms)
				return 0;
		}
	}

	return 1;
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

void markQueueStopped(agent *a) {
	for (struct queue *q = server.queueTable; q != NULL; q = q->hh.next) {
		if (q->agent == a) {
			print_msg(JERS_LOG_DEBUG, "Disabling queue %s", q->name);
			q->agent = NULL;
			q->state &= ~JERS_QUEUE_FLAG_STARTED;
		}
	}
}

int queueToJSON(struct queue *q, buff_t *buff)
{
	JSONStart(buff);
	JSONStartObject(buff, "QUEUE", 5);

	JSONAddString(buff, QUEUENAME, q->name);

	if (q->desc)
		JSONAddString(buff, DESC, q->desc);

	JSONAddString(buff, NODE, q->host);
	JSONAddInt(buff, JOBLIMIT, q->job_limit);
	JSONAddInt(buff, STATE, q->state);
	JSONAddInt(buff, PRIORITY, q->priority);
	JSONAddBool(buff, DEFAULT, (server.defaultQueue == q));

	JSONAddInt(buff, STATSRUNNING, q->stats.running);
	JSONAddInt(buff, STATSPENDING, q->stats.pending);
	JSONAddInt(buff, STATSHOLDING, q->stats.holding);
	JSONAddInt(buff, STATSDEFERRED, q->stats.deferred);
	JSONAddInt(buff, STATSCOMPLETED, q->stats.completed);
	JSONAddInt(buff, STATSEXITED, q->stats.exited);

	JSONEndObject(buff);
	JSONEnd(buff);
	return 0;
}
