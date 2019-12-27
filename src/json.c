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

static const char *JSONescapeString(const char *string, size_t size);

int JSONAddInt(buff_t *buff, int field_no, int64_t value)
{
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

int JSONAddStringN(buff_t *buff, int field_no, const char *value, size_t value_len)
{
	const char *field_name = getFieldName(field_no);
	size_t name_len = strlen(field_name);

	if (value == NULL) {
		value_len = 4; // 4 = null
	} else {
		value = JSONescapeString(value, value_len);
		value_len = strlen(value);
	}

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

	if (value)
	{
		p[len++] = '"';
		memcpy(p + len, value, value_len);
		len += value_len;
		p[len++] = '"';
	}
	else
	{
		memcpy(p + len, "null", 4);
		len += 4;
	}

	p[len++] = ',';

	buff->used += len;

	return 0;
}

int JSONAddString(buff_t *buff, int field_no, const char *value)
{
	return JSONAddStringN(buff, field_no, value, value ? strlen(value) : 0);
}

int JSONAddStringArray(buff_t *buff, int field_no, int64_t count, char **values)
{
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

	for (int64_t i = 0; i < count; i++)
	{
		size_t value_len = strlen(values[i]);

		if (i != 0)
			p[len++] = ',';

		p[len++] = '"';
		const char *escaped = JSONescapeString(values[i], 0);
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

int JSONAddBool(buff_t *buff, int field_no, int value)
{
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

int JSONAddMap(buff_t *buff, int field_no, int64_t count, key_val_t *values)
{
	const char *field_name = getFieldName(field_no);
	size_t required = 0;
	size_t len = 0;

	JSONStartObject(buff, field_name);
	for (int64_t i = 0; i < count; i++)
	{
		required += strlen(values[i].key);
		required += strlen(values[i].value);
		required += 6;
	}

	buffResize(buff, required);
	char *p = buff->data + buff->used;

	for (int64_t i = 0; i < count; i++)
	{
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

int JSONStartObject(buff_t *buff, const char *name)
{
	int name_len = name ? strlen(name) : 0;
	size_t required = name_len + 4;
	char *p = buff->data + buff->used;
	int len = 0;

	buffResize(buff, required);

	if (name) {
		p[len++] = '"';
		memcpy(p + len, name, name_len);
		len += name_len;
		p[len++] = '"';
		p[len++] = ':';
	}

	p[len++] = '{';
	buff->used += len;

	return 0;
}

int JSONEndObject(buff_t *buff)
{
	if (*(buff->data + buff->used - 1) == ',')
	{
		*(buff->data + buff->used - 1) = '}';
		buffAdd(buff, ",", 1);
	}
	else
	{
		buffAdd(buff, "},", 2);
	}

	return 0;
}

int JSONStartArray(buff_t *buff, const char *name)
{
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
	p[len++] = '[';
	buff->used += len;

	return 0;
}

int JSONEndArray(buff_t *buff)
{
	if (*(buff->data + buff->used - 1) == ',')
	{
		*(buff->data + buff->used - 1) = ']';
		buffAdd(buff, ",", 1);
	}
	else
	{
		buffAdd(buff, "],", 2);
	}

	return 0;
}

int JSONStart(buff_t *buf)
{
	buffAdd(buf, "{", 1);
	return 0;
}

int JSONEnd(buff_t *buf)
{
	/* Slight hack. We always append a ',' to the end of a field.
	 * We can just replace this with the '}', then add the newline */

	if (*(buf->data + buf->used - 1) == ',')
	{
		*(buf->data + buf->used - 1) = '}';
		buffAdd(buf, "\n", 1);
	}
	else
	{
		buffAdd(buf, "}\n", 2);
	}

	return 0;
}

static const char *JSONescapeString(const char *string, size_t size)
{
	static char *escaped = NULL;
	static size_t escaped_size = 0;
	size_t string_length = size == 0 ? strlen(string) : size;
	const char *temp = string;
	char *dest;

	/* Assume we have to escape everything */
	if (escaped_size <= string_length * 2)
	{
		escaped_size = string_length * 2;
		escaped = realloc(escaped, escaped_size);
	}

	dest = escaped;

	while (*temp != '\0')
	{
		switch (*temp)
		{
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

/* Return a pointer to after the opening of an object,
 * null terminating the ending '}' */

char *JSONGetObject(char **json) {
	char *pos = *json;
	char *start;
	int i = 0;
	int quoted = 0;

	pos = skipWhitespace(pos);

	if (*pos != '{')
		return NULL;

	pos++; /* Consume the '{' */
	start = pos;

	/* Find the trailing '}'
	 * Allow for {} characters embedded in quotes */
	while (*pos != '\0') {
		switch (*pos) {
			case '"':
				// Was it escaped?
				if (*(pos -1) == '\\')
					break;

				quoted ^= 1; // Flip quoted flag
				break;

			case '{':
				if (!quoted)
					i++;
				break;

			case '}':
				if (!quoted && i-- == 0)
					break;
		}

		if (i < 0)
			break;

		pos++;
	}

	if (*pos != '}')
		return NULL;

	*pos = '\0';
	pos++;

	*json = pos;
	return start;
}

/* Get and consume a JSON name, positioning the stream after the ':'
 * We might be at the end of a token, so we'll need to skip whitespace
 * and commas. */
char *JSONGetName(char **json) {
	char *name;
	char *pos = *json;
	pos = skipChars(pos, " \t,]}");

	if (JSONGetString(&pos, &name))
		return NULL;

	pos = skipChars(pos, " \t:");

	*json = pos;
	return name;
}

int JSONGetString(char **json, char **value) {
	char *string;
	char *pos = *json;
	char *dst = NULL;
	int modifying = 0;

	pos = skipWhitespace(pos);

	*value = NULL;

	if (*pos != '"') {
		/* Check if this is a null string */
		if (strncmp(pos, "null", 4) == 0) {
			*value = NULL;
			pos += 4;
			return 0;
		}

		return 1;
	}

	pos++; // Consume the "
	string = dst = pos;

	/* Find the closing quote, unescaping the string along the way,
	 * allowing for \" sequences */
	while (*pos != '\0') {
		if (*pos == '\\') {
			/* Escape sequence */
			pos++; /* Skip the \ */
			if (*pos == '\0')
				break;

			switch (*pos) {
				case '"': *dst++ = '"'; break;
				case '\\': *dst++ = '\\'; break;
				case 't': *dst++ = '\t'; break;
				case 'n': *dst++ = '\t'; break;
			}

			modifying = 1;
			pos++; /* Skip the next character */
			continue;
		}

		if (*pos == '"')
			break;

		if (modifying)
			*dst = *pos;

		dst++;
		pos++;
	}

	if (*pos != '"')
		return 1;

	/* NULL terminate and update the position in the provided string */
	*dst = '\0';
	*json = ++pos;
	*value = string;

	return 0;
}

static inline int64_t strtoint64(const char *str, int64_t *result) {
	/* Given str, read in an int64_t, returning the number of bytes read */
	*result = 0;
	char neg = 0;
	const char *pos = str;

	if (*pos == '-') {
		neg = 1;
		pos++;
	}

	while (*pos != '\0') {
		if (*pos < '0' || *pos > '9')
			break;

		*result = (*result * 10) + *pos - '0';
		pos++;
	}

	if (neg)
		*result = -(*result);

	return pos - str;
}

int JSONGetNum(char **json, int64_t *value) {
	char *pos = *json;

	pos = skipWhitespace(pos);
	pos += strtoint64(pos, value);
	*json = pos;

	return 0;
}

int JSONGetBool(char **json, char *value) {
	char *pos = *json;
	pos = skipWhitespace(pos);

	if (strncmp(pos, "true", 4) == 0) {
		*value = 1;
		pos += 4;
	}
	else if (strncmp(pos, "false", 5) == 0) {
		*value = 0;
		pos += 5;
	}
	else
		return 1;

	*json = pos;

	return 0;
}

int64_t JSONGetStringArray(char **json, char ***strings) {
	int64_t count = 0;
	int64_t max_strings = 8;
	char *pos = *json;
	char *str;

	/* Find the start and sanity check there is an end */
	pos = skipWhitespace(pos);

	if (*pos != '[')
		return -1;

	pos++; /* Consume the '[' */

	*strings = malloc(sizeof(char *) * max_strings);

	if (*strings == NULL)
		return -1;

	while (JSONGetString(&pos, &str) == 0) {
		/* Resize if needed */
		if (count >= max_strings) {
			*strings = realloc(*strings, sizeof(char *) * (max_strings *= 2));

			if (*strings == NULL)
				return -1;
		}

		(*strings)[count++] = str;

		/* Consume up until the next string */
		while (*pos != '\0' && *pos != ',' && *pos != ']') pos++;

		if (*pos == ']')
			break;

		if (*pos == ',')
			pos++;
	}

	pos++; /* Consume the ']' */
	*json = pos;

	return count;
}

int64_t JSONGetMap(char **json, key_val_t **map) {
	char *map_object;
	char *pos = *json;
	char *name;
	int64_t count = 0;
	int64_t max_keys = 8;

	/* A Map is object with key/values */
	map_object = JSONGetObject(&pos);

	if (map_object == NULL)
		return -1;

	/* Guess about how many object we will have */
	*map = malloc(sizeof(key_val_t) * max_keys);

	if (*map == NULL)
		return -1;

	while ((name = JSONGetName(&map_object)) != NULL) {
		char *value;

		if (JSONGetString(&map_object, &value))
			return -1;

		if (count >= max_keys) {
			*map = realloc(*map, sizeof(key_val_t) * (max_keys *= 2));

			if (*map == NULL)
				return -1;
		}

		(*map)[count].key = name;
		(*map)[count].value = value;

		count++;
	}

	*json = pos;

	return count;
}
