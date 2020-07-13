/* Copyright (c) 2020 Evan Wyatt
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

/* Index the passed in job */
void addIndexTag(struct job *j, char *tag_value) {
	/* Is this tag value already in the table? */
	struct indexed_tag *t = NULL;

	HASH_FIND_STR(server.index_tag_table, tag_value, t);

	if (t == NULL) {
		/* Create the values entry in the tag table */
		t = calloc(1, sizeof(struct indexed_tag));
		t->value = strdup(tag_value);

		HASH_ADD_STR(server.index_tag_table, value, t);
	}

	/* Add this job to the values job table */
	HASH_ADD(tag_hh, t->jobs, jobid, sizeof(int), j);
	j->index_table = t;
}

/* Remove a job from the tag index table */
void delIndexTag(struct job *j) {
	if (j->index_table == NULL)
		return;

	HASH_DELETE(tag_hh, j->index_table->jobs, j);
	j->index_table = NULL;
}
