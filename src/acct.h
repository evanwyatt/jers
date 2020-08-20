/* Copyright (c) 2018 Evan Wyatt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *	this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *	this list of conditions and the following disclaimer in the documentation
 *	and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 *	be used to endorse or promote products derived from this software without
 *	specific prior written permission.
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
#ifndef _ACCT_H
#define _ACCT_H

#include "comms.h"
#include "buffer.h"
#include "fields.h"

#include <server.h>

enum acctStates {
	ACCT_STOPPED = 0,
	ACCT_STARTED
};

typedef struct _acctClient {
	struct connectionType connection;

	buff_t request;
	size_t pos;

	buff_t response;
	size_t response_sent;

	uid_t uid;
	pid_t pid;

	int state;
	char *id;

	FILE *journal;
	off_t record;
	char datetime[10]; // YYYYMMDD

	int initalised;

	struct _acctClient * next;
	struct _acctClient * prev;
} acctClient;

struct journal_hdr {
	char saved;
	time_t timestamp_s;
	int timestamp_ms;
	uid_t uid;
	char command[65];
	jobid_t jobid;
	int64_t revision;
};

extern acctClient *acctClientList;

int handleAcctClientConnection(struct connectionType * connection);

void addAcctClient(acctClient *a);
void removeAcctClient(acctClient *a);
#endif
