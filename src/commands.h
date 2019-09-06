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

#include "server.h"
#include "cmd_defs.h"

int sendClientReturnCode(client *c, jers_object * obj, const char *ret);
int sendClientMessage(client *c, jers_object *obj, resp_t *r);
void sendAgentMessage(agent * a, resp_t * r);
void sendError(client * c, int error, const char * msg);

void replayCommand(msg_t * msg);

typedef struct command {
	char * name;
	int perm;     // Bitmask of required permissions
	int flags;    // Bitmask of command flags
	int (*cmd_func)(client * c, void *);
	void * (*deserialize_func)(msg_t *);
	void (*free_func)(void *, int);
} command_t;

typedef struct {
	char * name;
	int flags;
	int32_t (*cmd_func)(agent * a, msg_t * msg);
} agent_command_t;

int command_agent_login(agent * a, msg_t * msg);
int command_agent_jobstart(agent * a, msg_t * msg);
int command_agent_jobcompleted(agent * a, msg_t * msg);
int command_agent_recon(agent * a, msg_t * msg);
int command_agent_authresp(agent * a, msg_t * msg);
int command_agent_proxyconn(agent * a, msg_t * msg);
int command_agent_proxydata(agent * a, msg_t * msg);
int command_agent_proxyclose(agent * a, msg_t * msg);

int command_stats(client *, void *);

int command_add_job(client *, void *);
int command_get_job(client *, void *);
int command_mod_job(client *, void *);
int command_del_job(client *, void *);
int command_sig_job(client *, void *);


int command_add_queue(client *, void *);
int command_get_queue(client *, void *);
int command_mod_queue(client *, void *);
int command_del_queue(client *, void *);

int command_add_resource(client *, void *);
int command_get_resource(client *, void *);
int command_mod_resource(client *, void *);
int command_del_resource(client *, void *);

int command_set_tag(client *, void *);
int command_del_tag(client *, void *);

int command_get_agent(client *, void *);

void* deserialize_add_job(msg_t *);
void* deserialize_get_job(msg_t *);
void* deserialize_mod_job(msg_t *);
void* deserialize_del_job(msg_t *);
void* deserialize_sig_job(msg_t *);
void* deserialize_add_queue(msg_t *);
void* deserialize_get_queue(msg_t *);
void* deserialize_mod_queue(msg_t *);
void* deserialize_del_queue(msg_t *);
void* deserialize_add_resource(msg_t *);
void* deserialize_get_resource(msg_t *);
void* deserialize_mod_resource(msg_t *);
void* deserialize_del_resource(msg_t *);
void* deserialize_set_tag(msg_t *);
void* deserialize_del_tag(msg_t *);
void* deserialize_get_agent(msg_t *);

void free_add_job(void *, int);
void free_get_job(void *, int);
void free_mod_job(void *, int);
void free_del_job(void *, int);
void free_sig_job(void *, int);

void free_add_queue(void *, int);
void free_get_queue(void *, int);
void free_mod_queue(void *, int);
void free_del_queue(void *, int);

void free_add_resource(void *, int);
void free_get_resource(void *, int);
void free_mod_resource(void *, int);
void free_del_resource(void *, int);

void free_set_tag(void *, int);
void free_del_tag(void *, int);

void free_get_agent(void *, int);

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

/* Internal command structures */

typedef struct {
	jobid_t jobid;
	int signum;
} jersJobSig;

typedef struct {
	jobid_t jobid;
} jersJobDel;

typedef struct {
	jobid_t jobid;
	char * key;
	char * value;
} jersTagSet;

typedef struct {
	jobid_t jobid;
	char * key;
} jersTagDel;
