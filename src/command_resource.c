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
		appendError(c, "-INVARG Invalid name\n");
		return 1;
	}

	HASH_FIND_STR(server.resTable, ra->name, r);

	if (r != NULL) {
		appendError(c, "-INVARG Resource already exists\n");
		return 1;
	}

	r = calloc(sizeof(struct resource), 1);

	r->name = ra->name;
	r->count = ra->count;

	addRes(r, 1);

	resp_t * response = respNew();

	respAddSimpleString(response, "0");

	size_t reply_length = 0;
	char * reply = respFinish(response, &reply_length);
	appendResponse(c, reply, reply_length);
	free(reply);

	return 0;
}

int command_get_resource(client *c, void *args) {
	jersResourceFilter * rf = args;
	struct resource * r = NULL;
	int wildcard = 0;
	resp_t * response = NULL;

	if (rf->filters.name == NULL) {
		appendError(c, "-INVARG No name filter provided\n");
		return 1;
	}

	response = respNew();
	respAddArray(response);
	respAddSimpleString(response, "RESP");
	respAddInt(response, 1);

	wildcard = ((strchr(rf->filters.name, '*')) || (strchr(rf->filters.name, '?')));

	if (wildcard) {
		respAddArray(response);

		for (r = server.resTable; r != NULL; r = r->hh.next) {
			if (matches(rf->filters.name, r->name) == 0) {
				respAddMap(response);
				addStringField(response, RESNAME, r->name);
				addIntField(response, RESCOUNT, r->count);
				addIntField(response, RESINUSE, r->in_use);
				respCloseMap(response);	
			} 
		}

		respCloseArray(response);
	} else {
		HASH_FIND_STR(server.resTable, rf->filters.name, r);

		if (r) {
			respAddMap(response);
			addStringField(response, RESNAME, r->name);
			addIntField(response, RESCOUNT, r->count);
			addIntField(response, RESINUSE, r->in_use);
			respCloseMap(response);
		}
	}
	
	respCloseArray(response);

	size_t reply_length = 0;
	char * reply = respFinish(response, &reply_length);
	appendResponse(c, reply, reply_length);
	free(reply);

	return 0;
}

int command_mod_resource(client *c, void *args) {
	jersResourceMod * rm = args;
	struct resource * r = NULL;

	if (rm->name == NULL) {
		appendError(c, "-INVARG Resource name not provided\n");
		return 1;
	}

	HASH_FIND_STR(server.resTable, rm->name , r);

	if (r == NULL) {
		appendError(c, "-INVARG Resource not found\n");
		return 1;
	}

	r->count = rm->count;

	resp_t * response = respNew();
	respAddSimpleString(response, "0");
	size_t reply_length = 0;
	char * reply = respFinish(response, &reply_length);
	appendResponse(c, reply, reply_length);
	free(reply);

	return 0;
}

int command_del_resource(client *c, void *args) {
	return 0;
}

void free_add_resource(void * args) {
	jersResourceAdd * ra = args;
	free(ra);
}

void free_get_resource(void * args) {
	jersResourceFilter * rf = args;

	free(rf->filters.name);
	free(rf);
}

void free_mod_resource(void * args) {
	jersResourceMod * rm = args;
	free(rm);
}

void free_del_resource(void * args) {
	jersResourceDel * rd = args;

	free(rd->name);
	free(rd);
}
