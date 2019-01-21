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

#include <unistd.h>
#include <sys/types.h>

#include "server.h"
#include "common.h"
#include "logging.h"

/* Load a users permissions based on groups in the config file */
static void loadPermissions(struct user * u) {
	u->permissions = 0;

	for (int i = 0; i < u->group_count; i++) {
		gid_t g = u->group_list[i];

		for (int j = 0; j < server.permissions.read.count; j++) {
			if (g == server.permissions.read.groups[j]) {
				u->permissions |= PERM_READ;
				break;
			}
		}

		for (int j = 0; j < server.permissions.write.count; j++) {
			if (g == server.permissions.write.groups[j]) {
				u->permissions |= PERM_WRITE;
				break;
			}
		}

		for (int j = 0; j < server.permissions.setuid.count; j++) {
			if (g == server.permissions.setuid.groups[j]) {
				u->permissions |= PERM_SETUID;
				break;
			}
		}

		for (int j = 0; j < server.permissions.queue.count; j++) {
			if (g == server.permissions.queue.groups[j]) {
				u->permissions |= PERM_QUEUE;
				break;
			}
		}
	}
}

/* Lookup the user, checking if they have the requested permissions.
 * Returns:
 * 0 = Authorized
 * Non-zero = not authorized */

int validateUserAction(client * c, int required_perm) {
	if (c->uid == 0)
		return 0;

	c->user = lookup_user(c->uid, 0);

	if (c->user == NULL) {
		print_msg(JERS_LOG_WARNING, "Failed to find user uid %ld for permissions lookup", c->uid);
		return 1;
	}

	if (c->user->permissions == -1)
		loadPermissions(c->user);

	if ((c->user->permissions &required_perm) != required_perm)
		return 1;

	return 0;
}
