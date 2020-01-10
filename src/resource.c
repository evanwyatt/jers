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

#include <jers.h>
#include <server.h>

#include <json.h>

int addRes(struct resource * r, int dirty) {
	HASH_ADD_STR(server.resTable, name, r);

	r->obj.type = JERS_OBJECT_RESOURCE;
	updateObject(&r->obj, dirty);
	return 0;
}

void freeRes(struct resource * r) {
	free(r->name);
	free(r);
}

struct resource * findResource(char * name) {
	struct resource * r = NULL;
	HASH_FIND_STR(server.resTable, name, r);
	return r;
}

/* Cleanup resources that are marked as deleted, returning the number of resources cleaned up
 * - Only cleanup resources until the max_clean threshold is reached. */

int cleanupResources(uint32_t max_clean) {
	uint32_t cleaned_up = 0;
	struct resource *r, *tmp;

	if (max_clean == 0)
		max_clean = 10;

	HASH_ITER(hh, server.resTable, r, tmp) {
		if (!(r->internal_state &JERS_FLAG_DELETED))
			continue;

		/* Don't clean up resources flagged dirty or as being flushed */
		if (r->obj.dirty || r->internal_state &JERS_FLAG_FLUSHING)
			continue;

		/* Got a resources to remove */
		print_msg(JERS_LOG_DEBUG, "Removing deleted resource: %s", r->name);

		stateDelResource(r);
		HASH_DEL(server.resTable, r);
		freeRes(r);

		if (++cleaned_up >= max_clean)
			break;
	}

	return cleaned_up;
}

char ** convertResourceToStrings(int res_count, struct jobResource * res) {
	char ** res_strings = NULL;
	int i;

	res_strings = malloc(sizeof(char *) * res_count);

	for (i = 0; i < res_count; i++) {
		res_strings[i] = malloc(strlen(res[i].res->name) + 16);
		sprintf(res_strings[i], "%s:%d", res[i].res->name, res[i].needed);
	}

	return res_strings; 
}

int resourceToJSON(struct resource *r, buff_t *buff)
{
	JSONStart(buff);
	JSONStartObject(buff, "RESOURCE", 8);

	JSONAddString(buff, RESNAME, r->name);
	JSONAddInt(buff, RESCOUNT, r->count);
	JSONAddInt(buff, RESINUSE, r->in_use);

	JSONEndObject(buff);
	JSONEnd(buff);
	return 0;
}