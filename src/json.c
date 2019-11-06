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
#include <fields.h>
#include <json.h>

static int JSONStartObject(buff_t *buff, const char *name);
static int JSONEndObject(buff_t *buff);
static const char *JSONescapeString(const char *string);

static int JSONAddInt(buff_t *buff, int field_no, int64_t value) {
	const char *field_name = getFieldName(field_no);
	size_t name_len = strlen(field_name);

	/* Ensure we have room for '"field_name":value,' We assume we need 32 characters for the value*/
	size_t required = name_len + 32 + 4;
	buffResize(buff, required);

	int len = 0;
	char *p = buff->data + buff->used;

	p[len++] = '"';
	memcpy(p + len, field_name, name_len);
	len += name_len;
	p[len++] = '"';
	p[len++] = ':';
	len += int64tostr(p + len, value);
	p[len++] = ',';

	buff->used += len;

	return 0;
}

static int JSONAddString(buff_t *buff, int field_no, const char *value) {
	const char *field_name = getFieldName(field_no);
	size_t name_len = strlen(field_name);
	value = value ? JSONescapeString(value) : value;
	size_t value_len = value ? strlen(value) : 4; // 4 = null

	/* Ensure we have room for '"field_name":"value",' */
	size_t required = name_len + value_len + 6;
	buffResize(buff, required);

	int len = 0;
	char *p = buff->data + buff->used;

	p[len++] = '"';
	memcpy(p + len, field_name, name_len);
	len += name_len;
	p[len++] = '"';
	p[len++] = ':';

	if (value) {
		p[len++] = '"';
		memcpy(p + len, value, value_len);
		len += value_len;
		p[len++] = '"';
	} else {
		memcpy(p + len, "null", 4);
		len += 4;
	}

	p[len++] = ',';

	buff->used += len;

	return 0;
}

static int JSONAddStringArray(buff_t *buff, int field_no, int64_t count, char **values) {
	const char *field_name = getFieldName(field_no);
	size_t name_len = strlen(field_name);
	size_t required = name_len + 6;

	if (count == 0)
		return 0;

	for (int64_t i = 0; i < count; i++)
		required += (strlen(values[i]) * 2) + 3;

	buffResize(buff, required);

	char *p = buff->data + buff->used;
	int len = 0;

	p[len++] = '"';
	memcpy(p + len, field_name, name_len);
	len += name_len;
	p[len++] = '"';
	p[len++] = ':';
	p[len++] = '[';
	
	for (int64_t i = 0; i < count; i++) {
		size_t value_len = strlen(values[i]);

		if (i != 0)
			p[len++] = ',';

		p[len++] = '"';
		const char *escaped = JSONescapeString(values[i]);
		value_len = strlen(escaped);
		memcpy(p + len, escaped, value_len);
		len += value_len;
		p[len++] = '"';
	}

	p[len++] = ']';
	p[len++] = ',';

	buff->used += len;
	
	return 0;
}

static int JSONAddBool(buff_t *buff, int field_no, int value) {
	const char *field_name = getFieldName(field_no);
	size_t name_len = strlen(field_name);
	size_t required = name_len + 8;

	buffResize(buff, required);
	char *p = buff->data + buff->used;
	int len = 0;
	char *v = value ? "true" : "false";

	p[len++] = '"';
	memcpy(p + len, field_name, name_len);
	len += name_len;
	p[len++] = '"';
	p[len++] = ':';
	memcpy(p + len, v, strlen(v));
	len += strlen(v);
	p[len++] = ',';

	buff->used += len;

	return 0;
}

static int JSONAddMap(buff_t *buff, int field_no, int64_t count, key_val_t *values) {
	const char *field_name = getFieldName(field_no);
	size_t required = 0;
	size_t len = 0;

	JSONStartObject(buff, field_name);
	for (int64_t i = 0; i < count; i++) {
		required += strlen(values[i].key);
		required += strlen(values[i].value);
		required += 6;
	}

	buffResize(buff, required);
	char *p = buff->data + buff->used;

	for (int64_t i = 0; i < count; i++) {
		size_t key_len = strlen(values[i].key);
		size_t value_len = strlen(values[i].value);
		p[len++] = '"';
		memcpy(p + len, values[i].key, key_len);
		len += key_len;
		p[len++] = '"';
		p[len++] = ':';

		p[len++] = '"';
		memcpy(p + len, values[i].value, value_len);
		len += value_len;
		p[len++] = '"';
		p[len++] = ',';
	}

	buff->used += len;

	JSONEndObject(buff);

	return 0;
}

static int JSONStartObject(buff_t *buff, const char *name) {
	int name_len = strlen(name);
	size_t required = name_len + 4;
	char *p = buff->data + buff->used;
	int len = 0;

	buffResize(buff, required);
	p[len++] = '"';
	memcpy(p + len, name, name_len);
	len += name_len;
	p[len++] = '"';
	p[len++] = ':';
	p[len++] = '{';
	buff->used += len;

	return 0;
}

static int JSONEndObject(buff_t *buff) {
	if (*(buff->data + buff->used - 1) == ',') {
		*(buff->data + buff->used -1 ) = '}';
		buffAdd(buff, ",", 1);
	} else {
		buffAdd(buff, "},", 2);
	}

	return 0;
}

static int JSONStart(buff_t *buf) {
	buffAdd(buf, "{", 1);
	return 0;
}

static int JSONEnd(buff_t *buf) {
	/* Slight hack. We always append a ',' to the end of a field.
	 * We can just replace this with the '}', then add the newline */

	if (*(buf->data + buf->used - 1) == ',') {
		*(buf->data + buf->used -1 ) = '}';
		buffAdd(buf, "\n", 1);
	} else {
		buffAdd(buf, "}\n", 2);
	}

	return 0;
}

//HACK:
static char ** _convertResourceToStrings(int res_count, struct jobResource * res) {
	char ** res_strings = NULL;
	int i;

	res_strings = malloc(sizeof(char *) * res_count);


	for (i = 0; i < res_count; i++) {
		res_strings[i] = malloc(strlen(res[i].res->name) + 16);
		sprintf(res_strings[i], "%s:%d", res[i].res->name, res[i].needed);
	}

	return res_strings; 
}

static const char * JSONescapeString(const char *string) {
	static char * escaped = NULL;
	static size_t escaped_size = 0;
	size_t string_length = strlen(string);
	const char * temp = string;
	char * dest;

	/* Assume we have to escape everything */
	if (escaped_size <= string_length * 2 ) {
		escaped_size = string_length *2;
		escaped = realloc(escaped, escaped_size);
	}

	dest = escaped;

	while (*temp != '\0') {
		switch (*temp) {
			case '\\':
				*dest++ = '\\';
				*dest++ = '\\';
				break;

			case '\t':
				*dest++ = '\\';
				*dest++ = 't';
				break;

			case '\n':
				*dest++ = '\\';
				*dest++ = 'n';
				break;

			case '"':
				*dest++ = '\\';
				*dest++ = '"';
				break;

			default:
				*dest++ = *temp;
				break;
		}

		temp++;
	}

	*dest = '\0';
	return escaped;
}

/* Convert a JERS object to json */
int jobToJSON(struct job *j, buff_t *buff) {
	JSONStart(buff);
	JSONStartObject(buff, "JOB");

    JSONAddInt(buff, JOBID, j->jobid);
    JSONAddString(buff, JOBNAME, j->jobname);
    JSONAddString(buff, QUEUENAME, j->queue->name);
    JSONAddInt(buff, STATE, j->state);
    JSONAddInt(buff, UID, j->uid);
    JSONAddInt(buff, SUBMITTER,j->submitter);
    JSONAddInt(buff, PRIORITY, j->priority);
    JSONAddInt(buff, SUBMITTIME, j->submit_time);
    JSONAddInt(buff, NICE, j->nice);
    JSONAddStringArray(buff, ARGS, j->argc, j->argv);
    JSONAddString(buff, NODE, j->queue->host);
    JSONAddString(buff, STDOUT, j->stdout);
    JSONAddString(buff, STDERR, j->stderr);	

	if (j->defer_time)
		JSONAddInt(buff, DEFERTIME, j->defer_time);

	if (j->start_time)
		JSONAddInt(buff, STARTTIME, j->start_time);

	if (j->finish_time)
		JSONAddInt(buff, FINISHTIME, j->finish_time);

	if (j->tag_count)
		JSONAddMap(buff, TAGS, j->tag_count, j->tags);

	if (j->shell)
		JSONAddString(buff, SHELL, j->shell);	

	if (j->pre_cmd)
		JSONAddString(buff, POSTCMD, j->pre_cmd);

	if (j->post_cmd)
		JSONAddString(buff, PRECMD, j->post_cmd);

	if (j->res_count) {
        char ** res_strings = _convertResourceToStrings(j->res_count, j->req_resources);
        JSONAddStringArray(buff, RESOURCES, j->res_count, res_strings);

        for (int i = 0; i < j->res_count; i++) {
            free(res_strings[i]);
        }

        free(res_strings);
	}

	if (j->pid)
		JSONAddInt(buff, JOBPID, j->pid);

	JSONAddInt(buff, EXITCODE, j->exitcode);
	JSONAddInt(buff, SIGNAL, j->signal);

	if (j->pend_reason)
		JSONAddInt(buff, PENDREASON, j->pend_reason);

	if (j->fail_reason)
		JSONAddInt(buff, FAILREASON, j->fail_reason);

	JSONEndObject(buff);
	JSONEnd(buff);

	return 0;
}


int queueToJSON(struct queue *q, buff_t *buff) {
	JSONStart(buff);
	JSONStartObject(buff, "QUEUE");

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


int resourceToJSON(struct resource *r, buff_t *buff) {
	JSONStart(buff);
	JSONStartObject(buff, "RESOURCE");

	JSONAddString(buff, RESNAME, r->name);
	JSONAddInt(buff, RESCOUNT, r->count);
	JSONAddInt(buff, RESINUSE, r->in_use);

	JSONEndObject(buff);
	JSONEnd(buff);
	return 0;
}

/* Convert a Msg to JSON */

int msgToJSON(struct journal_hdr *hdr, msg_t *msg, int64_t id, buff_t *buff) {
	JSONStart(buff);
	JSONStartObject(buff, msg->command);

	/* Add in the header */
	if (id)
		JSONAddInt(buff, ACCT_ID, id);

	if (hdr->jobid)
		JSONAddInt(buff, JOBID, hdr->jobid);

	JSONAddInt(buff, UID, hdr->uid);

	JSONStartObject(buff, "FIELDS");

	for (int64_t i = 0; i < msg->items[0].field_count; i++) {
		switch(msg->items[0].fields[i].type) {
			case RESP_TYPE_BLOBSTRING:
				JSONAddString(buff, msg->items[0].fields[i].number, msg->items[0].fields[i].value.string.value);
				break;
			case RESP_TYPE_INT:
				JSONAddInt(buff, msg->items[0].fields[i].number, msg->items[0].fields[i].value.number);
				break;
			case RESP_TYPE_BOOL:
				JSONAddBool(buff, msg->items[0].fields[i].number, msg->items[0].fields[i].value.boolean);
				break;

			case RESP_TYPE_ARRAY:
				JSONAddStringArray(buff, msg->items[0].fields[i].number, msg->items[0].fields[i].value.string_array.count, msg->items[0].fields[i].value.string_array.strings);
				break;

			case RESP_TYPE_MAP:
				JSONAddMap(buff, msg->items[0].fields[i].number, msg->items[0].fields[i].value.map.count, msg->items[0].fields[i].value.map.keys);
				break;
		}
	}

	JSONEndObject(buff);
	JSONEndObject(buff);
	JSONEnd(buff);
	return 0;
}

#ifdef _MAIN
int main (int argc, char *argv[]) {
	printf("Testing JSON...\n");

	buff_t buff;
	buffNew(&buff, 1);

	JSONStart(&buff);

	JSONAddInt(&buff, JOBID, 1234);
	JSONAddString(&buff,JOBNAME, "Test batch job");
	JSONAddBool(&buff, HOLD, 1);
	char *args[] = {"arg1", "arg2", "arg t h r e e"};
	JSONAddStringArray(&buff, ARGS, 3, args);

	JSONStartObject(&buff, "FIELDS");

	JSONAddInt(&buff, JOBPID, 1111111);
	JSONAddString(&buff, DESC, "My desc here");

	JSONAddBool(&buff, STATE, 0);

	key_val_t map[] = {{"key1", "value"},{"key 2", "V A L U E"}};
	JSONAddMap(&buff, TAGS, 2, map);

	JSONAddBool(&buff, RESTART, 1);


	JSONEndObject(&buff);
	JSONEnd(&buff);

	printf("Result: %.*s\n", buff.used, buff.data);

	buffFree(&buff);

	return 0;
}

#endif