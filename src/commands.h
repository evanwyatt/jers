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

void appendResponse(client * c, char * buffer, size_t length);
void appendError(client * c, char * msg);

typedef struct command {
	char * name;
	int (*cmd_func)(client * c, void *);
	void * (*deserialize_func)(msg_t *);
	void * (*free_func)(void *);
} command_t;


int command_add_job(client *, void*);
int command_get_job(client *, void*);
int command_mod_job(client *, void*);
int command_del_job(client *, void*);

int command_add_queue(client *, void*);
int command_get_queue(client *, void*);
int command_mod_queue(client *, void*);
int command_del_queue(client *, void*);

int command_add_resource(client *, void*);
int command_get_resource(client *, void*);
int command_mod_resource(client *, void*);
int command_del_resource(client *, void*);

void* deserialize_add_job(msg_t *);
void* deserialize_get_job(msg_t *);
void* deserialize_mod_job(msg_t *);
void* deserialize_del_job(msg_t *);
void* deserialize_add_queue(msg_t *);
void* deserialize_get_queue(msg_t *);
void* deserialize_mod_queue(msg_t *);
void* deserialize_del_queue(msg_t *);
void* deserialize_add_resource(msg_t *);
void* deserialize_get_resource(msg_t *);
void* deserialize_mod_resource(msg_t *);
void* deserialize_del_resource(msg_t *);

/* Magic values */

#define JOBADD_MAGIC 0x4a4a414d
#define JOBGET_MAGIC 0x4a4a474d
#define JOBMOD_MAGIC 0x4a4a4d4d
#define JOBDEL_MAGIC 0x4a4a444d

#define QUEUEADD_MAGIC 0x4a51414d 
#define QUEUEGET_MAGIC 0x4a51474d
#define QUEUEMOD_MAGIC 0x4a514d4d
#define QUEUEDEL_MAGIC 0x4a51444d

#define RESADD_MAGIC 0x4a52414d
#define RESGET_MAGIC 0x4a52474d
#define RESMOD_MAGIC 0x4a524d4d
#define RESDEL_MAGIC 0x4a52444d

