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

#include <fnmatch.h>

void * deserialize_add_resource(msg_t * msg) {
	jersResourceAdd * ra = calloc(sizeof(jersResourceAdd), 1);

	msg_item * item = &msg->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case RESNAME  : ra->name = getStringField(&item->fields[i]); break;
			case RESCOUNT : ra->count = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	if (ra->count == 0)
		ra->count = 1;

	return ra;
}

void * deserialize_get_resource(msg_t * msg) {
	jersResourceFilter * rf = calloc(sizeof(jersResourceFilter), 1);

	msg_item * item = &msg->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case RESNAME: rf->filters.name = getStringField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n", item->fields[i].name); break;
		}
	}

	return rf;
}

void * deserialize_mod_resource(msg_t * msg) {
	jersResourceMod * rm = calloc(sizeof(jersResourceMod), 1);
	msg_item * item = &msg->items[0];
	int i;

	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case RESNAME : rm->name = getStringField(&item->fields[i]); break;
			case RESCOUNT: rm->count = getNumberField(&item->fields[i]); break;

			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break; 
		}
	}

	return rm;
}

void * deserialize_del_resource(msg_t * msg) {
	jersResourceDel * rd = calloc(sizeof(jersResourceDel), 1);
	msg_item * item = &msg->items[0];
	int i;
	
	for (i = 0; i < item->field_count; i++) {
		switch(item->fields[i].number) {
			case RESNAME : rd->name = getStringField(&item->fields[i]); break;
	
			default: fprintf(stderr, "Unknown field '%s' encountered - Ignoring\n",item->fields[i].name); break;
		}
	}

	return rd;
}

int command_add_resource(client *c, void *args) {
	jersResourceAdd * ra = args;
	struct resource * r = NULL;

	if (ra->name == NULL) {
		sendError(c, JERS_ERR_INVARG, "No resource name provided");
		return 1;
	}

	if (check_name(ra->name)) {
		sendError(c, JERS_ERR_INVARG, "Invalid resource name");
		return 1;
	}

	r = findResource(ra->name);

	if (r != NULL) {
		if (server.recovery.in_progress)
			return 0;

		if ((r->internal_state &JERS_FLAG_DELETED) == 0) {
			sendError(c, JERS_ERR_RESEXISTS, NULL);
			return 1;
		}

		/* We have a deleted version of this resource. Free the old name and reuse the resource */
		HASH_DEL(server.resTable, r);
		free(r->name);
		r->internal_state &= ~JERS_FLAG_DELETED;
	} else {
		r = calloc(sizeof(struct resource), 1);
	}

	r->name = ra->name;
	r->count = ra->count;
	r->revision = 1;

	addRes(r, 1);

	return sendClientReturnCode(c, "0");
}

int command_get_resource(client *c, void *args) {
	jersResourceFilter * rf = args;
	struct resource * r = NULL;
	int wildcard = 0;
	resp_t response;

	if (rf->filters.name == NULL) {
		sendError(c, JERS_ERR_INVARG, "No name filter provided");
		return 1;
	}

	initMessage(&response, "RESP", 1);

	wildcard = ((strchr(rf->filters.name, '*')) || (strchr(rf->filters.name, '?')));

	if (wildcard) {
		respAddArray(&response);

		for (r = server.resTable; r != NULL; r = r->hh.next) {
			if ((r->internal_state &JERS_FLAG_DELETED) == 0 && matches(rf->filters.name, r->name) == 0) {
				respAddMap(&response);
				addStringField(&response, RESNAME, r->name);
				addIntField(&response, RESCOUNT, r->count);
				addIntField(&response, RESINUSE, r->in_use);
				respCloseMap(&response);	
			} 
		}

		respCloseArray(&response);
	} else {
		r = findResource(rf->filters.name);

		if (r && (r->internal_state &JERS_FLAG_DELETED) == 0) {
			respAddMap(&response);
			addStringField(&response, RESNAME, r->name);
			addIntField(&response, RESCOUNT, r->count);
			addIntField(&response, RESINUSE, r->in_use);
			respCloseMap(&response);
		}
	}

	return sendClientMessage(c, &response);	
}

int command_mod_resource(client *c, void *args) {
	jersResourceMod * rm = args;
	struct resource * r = NULL;

	if (rm->name == NULL) {
		sendError(c, JERS_ERR_INVARG, "Resource name not provided");
		return 1;
	}

	r = findResource(rm->name);

	if (r == NULL || r->internal_state &JERS_FLAG_DELETED) {
		sendError(c, JERS_ERR_NORES, NULL);
		return 1;
	}

	if (unlikely(server.recovery.in_progress)) {
		if (r->revision >= server.recovery.revision) {
			print_msg(JERS_LOG_DEBUG, "Skipping recovery of resource_mod resource %s rev:%ld trans rev:%ld", r->name, r->revision, server.recovery.revision);
			return 0;
		}
	}

	r->count = rm->count;
	r->revision++;

	return sendClientReturnCode(c, "0");
}

int command_del_resource(client *c, void *args) {
	jersResourceDel * rd = args;
	struct resource * r = NULL;
	int in_use = 0;

	if (rd->name == NULL) {
		sendError(c, JERS_ERR_INVARG, "Resource name not provided");
		return 1;
	}
	
	r = findResource(rd->name);

	if (r == NULL || r->internal_state &JERS_FLAG_DELETED) {
		sendError(c, JERS_ERR_NORES, NULL);
		return 1;
	}

	/* Check that it's not in use. */
	for (struct job * j = server.jobTable; j != NULL; j = j->hh.next) {
		if (j->res_count == 0 || j->internal_state & JERS_FLAG_DELETED)
			continue;

		for (int i = 0; i < j->res_count; i++) {
			if (j->req_resources[i].res == r) {
				in_use = 1;
				break;
			}
		}

		if (in_use) {
			sendError(c, JERS_ERR_RESINUSE, NULL);
			return 1;
		}
	}

	/* Mark it as deleted and dirty. It will be cleaned up later */
	r->internal_state |= JERS_FLAG_DELETED;

	return sendClientReturnCode(c, "0");
}

void free_add_resource(void * args, int status) {
	jersResourceAdd * ra = args;

	if (status) {
		free(ra->name);
	}

	free(ra);
}

void free_get_resource(void * args, int status) {
	jersResourceFilter * rf = args;

	free(rf->filters.name);
	free(rf);
}

void free_mod_resource(void * args, int status) {
	jersResourceMod * rm = args;

	free(rm->name);
	free(rm);
}

void free_del_resource(void * args, int status) {
	jersResourceDel * rd = args;

	free(rd->name);
	free(rd);
}
