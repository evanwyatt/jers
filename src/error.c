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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include "server.h"

int jers_errno = JERS_ERR_OK;
char * jers_err_string = NULL;

void set_jers_errno();

struct jers_err {
	char * name;
	char * msg;
};

const struct jers_err jers_errors[] = {
	{"JERS_ERR_OK",          "Success"},
	{"JERS_ERR_NOJOB",       "Job not found"},
	{"JERS_ERR_NOPERM",      "Permission denied"},
	{"JERS_ERR_NOQUEUE",     "Queue not found"},
	{"JERS_ERR_INVARG",      "Invalid argument provided"},
	{"JERS_ERR_INVTAG",      "Invalid tag provided"},
	{"JERS_ERR_NOTAG",       "Tag not found"},
	{"JERS_ERR_JOBEXISTS",   "Job already exists"},
	{"JERS_ERR_RESEXISTS",   "Resource already exists"},
	{"JERS_ERR_QUEUEEXISTS", "Queue already exists"},
	{"JERS_ERR_NOTEMPTY",    "Queue is not empty"},
	{"JERS_ERR_NORES",       "Resource not found"},
	{"JERS_ERR_INIT",        "Failed to initalise"},
	{"JERS_ERR_NOCHANGE",    "Nothing to update"},
	{"JERS_ERR_INVSTATE",    "Invalid state"},
	{"JERS_ERR_MEM",         "Out of memory"},
	{"JERS_ERR_INVRESP",     "Invalid message received"},
	{"JERS_ERR_ERECV",       "Error receiving data from"},
	{"JERS_ERR_ESEND",       "Error sending data"},
	{"JERS_ERR_DISCONNECT",  "Disconnected from daemon"},

	{"JERS_ERR_UNKNOWN",     "Unknown error occurred"}
};

const char * jers_pend_reasons[] = {
	"",
	"System max jobs reached",
	"Queue limit reached",
	"Waiting for resources",
	"Queue stopped",
	"Agent not connected",
	"Waiting for agent to confirm start",
	"Agent starting",

	"Unknown reason"
};

const char * jers_fail_reasons[] = {
	"",
	"Failed to read completion information",
	"Failed to read PID of started job",
	"Failed to create log file",
	"Failed to create temporary script",
	"Failed to start job",
	"Failed to initalise job",
	"Got signal while starting job",

	"Unknown reason"
};

void setJersErrno(int err, char * msg) {
	int saved_errno = errno;
	jers_errno = err;
	
	free(jers_err_string);
	jers_err_string = NULL;

	if (msg)
		jers_err_string = strdup(msg);

	/* Set errno back to what it was before this routine was called */
	errno = saved_errno;
}

/* Translate the error string into the errno */
int lookup_jers_errno(const char * str) {
	static int err_count = sizeof(jers_errors) / sizeof(struct jers_err);

	for (int i = 0; i < err_count; i++) {
		if (strcmp(jers_errors[i].name, str) == 0)
			return i;
	}

	return JERS_ERR_UNKNOWN;
}

int getJersErrno(const char * error_string, char ** error_message) {
	char error_type[32];
	char msg[128] = "";

	if (sscanf(error_string, "%31s %127[^\n]", error_type, msg) < 1) {
		/* This is awkward. We have a error getting the error */
		if (error_message)
			*error_message = NULL;

		return JERS_ERR_UNKNOWN;
	}

	if (*msg && error_message)
		*error_message = strdup(msg);

	return lookup_jers_errno(error_type);
}

const char * getErrString(int jers_error) {
	if (jers_error < 0 || jers_error > JERS_ERR_UNKNOWN)
		return "Invalid jers_errno provided";

	return jers_err_string ? jers_err_string : jers_errors[jers_error].msg;
}

const char * getErrType(int jers_error) {
	if (jers_error < 0 || jers_error > JERS_ERR_UNKNOWN)
		return jers_errors[JERS_ERR_UNKNOWN].name;

	return jers_errors[jers_error].name;
}

const char * getPendString(int reason) {
	if (reason < 0 || reason > JERS_PEND_UNKNOWN)
		return "Invalid reason code provided";

	return jers_pend_reasons[reason];
}

const char * getFailString(int reason) {
	if (reason < 0 || reason > JERS_FAIL_UNKNOWN)
		return "Invalid reason code provided";

	return jers_fail_reasons[reason];
}
