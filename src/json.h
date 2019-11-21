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

#ifndef _JSON_H
#define _JSON_H

#include <buffer.h>
#include <fields.h>

int JSONStart(buff_t *buff);
int JSONEnd(buff_t *buff);
int JSONStartObject(buff_t *buff, const char *name);
int JSONEndObject(buff_t *buff);
int JSONStartArray(buff_t *buff, const char *name);
int JSONEndArray(buff_t *buff);

int JSONAddInt(buff_t *buff, int field_no, int64_t value);
int JSONAddString(buff_t *buff, int field_no, const char *value);
int JSONAddStringN(buff_t *buff, int field_no, const char *value, size_t len);
int JSONAddStringArray(buff_t *buff, int field_no, int64_t count, char **values);
int JSONAddBool(buff_t *buff, int field_no, int value);
int JSONAddMap(buff_t *buff, int field_no, int64_t count, key_val_t *values);

char *JSONGetObject(char **json);
char *JSONGetName(char **json);
int JSONGetString(char **json, char **value);
int JSONGetNum(char **json, int64_t *value);
int JSONGetBool(char **json, char *value);
int64_t JSONGetStringArray(char **json, char ***strings);
int64_t JSONGetMap(char **json, key_val_t **map);

#endif