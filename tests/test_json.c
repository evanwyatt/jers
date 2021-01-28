#include <stdio.h>

#include <jers_tests.h>
#include <json.h>

enum json_test_types {
	JSON_END,
	JSON_OBJ,
	JSON_END_OBJ,
	JSON_NAMED_OBJ,
	JSON_STRING,
	JSON_STRING_ARRAY,
	JSON_NUM,
	JSON_BOOL,
	JSON_MAP,
};

struct json_test {
	int test_type;
	char *name;

	union {
		char *string;
		int64_t num;
		char bool;
		struct {
			int64_t count;
			char **strings;
		} string_array;
		struct {
			int64_t count;
			key_val_t *maps;
		} map;

	} expected;
};

static int _test_json(struct json_test *t, char *json) {
	char *levels[32] = {json};
	int depth = 0;

	char *name;
	char *string;
	int64_t num;
	char bool;
	int64_t array_count;
	int64_t map_count;
	char **array;
	key_val_t *map;

	for (int i = 0; t[i].test_type != JSON_END; i++) {
		switch(t[i].test_type) {
			case JSON_OBJ:
				levels[depth + 1] = JSONGetObject(&levels[depth]);

				/* Should have a pointer to the obj */
				if (levels[depth + 1] == NULL) {
					printf("Expected JSON object.\n");
					return 1;
				}

				depth++;
			break;

			case JSON_END_OBJ:
				depth--;
				if (depth < 0) {
					printf("Test incorrect, depth < 0\n");
					return 1;
				}

				break;

			case JSON_NAMED_OBJ:
				name = JSONGetName(&levels[depth]);

				if (name == NULL || strcmp(name, (char *)t[i].name) != 0) {
					printf("Expected named obj. Got: '%s' Expected '%s'\n", name ? name : "NULL", (char *)t[i].name);
					return 1;
				}

				levels[depth + 1] = JSONGetObject(&levels[depth]);

				if (levels[depth + 1] == NULL) {
					printf("Expected JSON object.\n");
					return 1;
				}

				depth++;
				break;

			case JSON_STRING:
				name = JSONGetName(&levels[depth]);

				if (name == NULL || strcmp(name, t[i].name) != 0) {
					printf("Unexpected value when getting the fieldname for a string.\n");
					printf("Got: '%s' Expected: '%s'\n", name ? name : "NULL", t[i].name);
					return 1;
				}

				if (JSONGetString(&levels[depth], &string)) {
					printf("Failed to get string, expected: '%s'\n", t[i].expected.string);
					return 1;
				}

				if (t[i].expected.string == NULL) {
					if (string != NULL) {
						printf("Unexpected string value. Expected a null string, got: '%s'\n", string);
						return 1;
					}
				} else if (strcmp(string, t[i].expected.string) != 0) {
					printf("Unexpected string value. Got: '%s' Expected: '%s'\n", string, t[i].expected.string);
					return 1;
				}

				break;

			case JSON_STRING_ARRAY:
				name = JSONGetName(&levels[depth]);
				if (name == NULL || strcmp(name, t[i].name) != 0) {
					printf("Unexpected value when getting the fieldname for a string.\n");
					printf("Got: '%s' Expected: '%s'\n", name ? name : "NULL", t[i].name);
					return 1;
				}

				if ((array_count = JSONGetStringArray(&levels[depth], &array)) != t[i].expected.string_array.count) {
					printf("Unexpected array count when getting string array.\n");
					printf("Got: %ld Expected: %ld\n", array_count, t[i].expected.string_array.count);
					return 1;
				}

				if (array_count == 0) {
					/* Check that an empty array does not return a pointer */
					if (array != NULL) {
						printf("Empty array returned a pointer\n");
						return 1;
					}
				}

				for (int64_t count = 0; count < array_count; count++) {
					if (strcmp(array[count], t[i].expected.string_array.strings[count]) != 0) {
						printf("Unexpected array value index %ld.\n", count);
						printf("Got: '%s' Expected: '%s'\n", array[count], t[i].expected.string_array.strings[count]);
						return 1;
					}
				}

				free(array);

				break;

			case JSON_NUM:
				name = JSONGetName(&levels[depth]);
				if (name == NULL || strcmp(name, t[i].name) != 0) {
					printf("Unexpected value when getting the fieldname for a string.\n");
					printf("Got: '%s' Expected: '%s'\n", name ? name : "NULL", t[i].name);
					return 1;
				}

				if (JSONGetNum(&levels[depth], &num)) {
					printf("Failed to get number, expected %ld\n", t[i].expected.num);
					return 1;
				}

				if (num != t[i].expected.num) {
					printf("Unexpected number value. Got: %ld Expected: %ld\n", num, t[i].expected.num);
					return 1;
				}

				break;

			case JSON_BOOL:
				name = JSONGetName(&levels[depth]);
				if (name == NULL || strcmp(name, t[i].name) != 0) {
					printf("Unexpected value when getting the fieldname for a string.\n");
					printf("Got: '%s' Expected: '%s'\n", name ? name : "NULL", t[i].name);
					return 1;
				}

				if (JSONGetBool(&levels[depth], &bool)) {
					printf("Failed to get bool, expected %d\n", t[i].expected.bool);
					return 1;
				}

				if (bool != t[i].expected.bool) {
					printf("Unexpected bool value. Got: %d Expected: %d\n", bool, t[i].expected.bool);
					return 1;
				}

				break;

			case JSON_MAP:
				name = JSONGetName(&levels[depth]);
				if (name == NULL || strcmp(name, t[i].name) != 0) {
					printf("Unexpected value when getting the fieldname for a map.\n");
					printf("Got: '%s' Expected: '%s'\n", name ? name : "NULL", t[i].name);
					return 1;
				}

				if ((map_count = JSONGetMap(&levels[depth], &map)) != t[i].expected.map.count) {
					printf("Unexpected array count when getting map.\n");
					printf("Got: %ld Expected: %ld\n", map_count, t[i].expected.map.count);
					return 1;
				}

				if (map_count == 0) {
					/* Check that an empty map does not return a pointer */
					if (map != NULL) {
						printf("Empty map returned a pointer\n");
						return 1;
					}
				}

				for (int64_t count = 0; count < map_count; count++) {
					/* Compare the key */
					if (strcmp(map[count].key, t[i].expected.map.maps[count].key) != 0) {
						printf("Unexpected map key count:%ld.\n", count);
						printf("Got: '%s' Expected: '%s'\n", map[count].key, t[i].expected.map.maps[count].key);
						return 1;
					}

					/* Compare the value */
					if (strcmp(map[count].value, t[i].expected.map.maps[count].value) != 0) {
						printf("Unexpected map value. count:%ld.\n", count);
						printf("Got: '%s' Expected: '%s'\n", map[count].value, t[i].expected.map.maps[count].value);
						return 1;
					}
				}

				free(map);
				break;
		}
	}

	/* Should have consumed everything. */
	if (*levels[0] != '\0' && *levels[0] != '\n') {
		printf("More data left after test finish: '%s'\n", levels[0]);
		return 1;
	}

	return 0;
}


int cmp_json(buff_t *b, char *expected, size_t expected_len) {
	if (b->used != expected_len) {
		DEBUG("JSON length incorrect. Expected length: %ld, Used:%ld\n", expected_len, b->used);
		printf("JSON: '%.*s'\n", (int)b->used, b->data);
		printf("Expected: '%s'\n", expected);
		return 1;
	}

	if (memcmp(b->data, expected, expected_len) != 0) {
		DEBUG("JSON data incorrect.\n");
		printf("JSON: '%.*s'\n", (int)b->used, b->data);
		printf("Expected: '%s'\n", expected);
		return 1;
	}

	return 0;
}

int test_json1(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);

	return status;
}

int test_json2(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json3(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{},\"second_obj\":{}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONEndObject(&b);
	JSONStartObject(&b, "second_obj", strlen("second_obj"));
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json4(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"JOBID\":1234}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddInt(&b, JOBID, 1234);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json5(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"ARGS\":[\"Hello\",\"World\"]}}\n";
	size_t expected_len = strlen(expected);
	char *args[] = {"Hello", "World"};

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddStringArray(&b, ARGS, 2, args);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json5_2(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"ARGS\":[]}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddStringArray(&b, ARGS, 0, NULL);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json6(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"ARGS\":[\"Hello\",\"World\"],\"JOBID\":1234}}\n";
	size_t expected_len = strlen(expected);
	char *args[] = {"Hello", "World"};

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddStringArray(&b, ARGS, 2, args);
	JSONAddInt(&b, JOBID, 1234);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json7(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"JOBNAME\":\"Hello \\\"World\\\"\"}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddString(&b, JOBNAME, "Hello \"World\"");
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json8(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"JOBNAME\":\"\\\\Hello\\\\\\t\\\"World\\\"\\n\"}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddString(&b, JOBNAME, "\\Hello\\\t\"World\"\n");
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json9(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"TAGS\":{\"key_1\":\"value \\\"one\\\"\",\"key_2\":\"value two\"}}}\n";
	size_t expected_len = strlen(expected);

	key_val_t tags[2] = {
		{"key_1", "value \"one\""},
		{"key_2", "value two"}
	};

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddMap(&b, TAGS, 2, tags);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json9_2(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"TAGS\":{}}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddMap(&b, TAGS, 0, NULL);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json10(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"JOBNAME\":\"The quick brown fox\"}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddStringN(&b, JOBNAME, "The quick brown fox jumps over the lazy dog", 19);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}
int test_json11(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"JOBNAME\":null}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddString(&b, JOBNAME, NULL);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

int test_json12(void) {
	int status = 0;
	buff_t b = {0};
	char *expected = "{\"test_obj\":{\"HOLD\":true,\"RESTART\":false}}\n";
	size_t expected_len = strlen(expected);

	JSONStart(&b);
	JSONStartObject(&b, "test_obj", strlen("test_obj"));
	JSONAddBool(&b, HOLD, 1);
	JSONAddBool(&b, RESTART, 0);
	JSONEndObject(&b);
	JSONEnd(&b);

	status = cmp_json(&b, expected, expected_len);
	buffFree(&b);
	return status;
}

/* Simple JSON parsing tests */

int test_json13(void) {
	char json[] = "{\"test_obj\":{}}\n";
	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json14(void) {
	char json[] = "{\"test_obj\":{\"JOBNAME\":\"Hello World\"}}\n";

	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_STRING, "JOBNAME", {.string = "Hello World"}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json15(void) {
	char json[] = "{\"test_obj\":{\"JOBNAME\":\"Hello}\\tWorld\\n\"}}";
	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_STRING, "JOBNAME", {.string = "Hello}\tWorld\n"}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json16(void) {
	char json[] = "{\"test_obj\":{\"JOBID\": 12345}}";
	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_NUM, "JOBID", {.num = 12345}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json17(void) {
	char json[] = "{\"test_obj\":{\"JOBNAME\": null}}";
	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_STRING, "JOBNAME", {.string = NULL}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json18(void) {
	char json[] = "{\"test_obj\":{\"HOLD\": true, \"HOLD\": false}}";
	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_BOOL, "HOLD", {.bool = 1}},
		{JSON_BOOL, "HOLD", {.bool = 0}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json19(void) {
	char json[] = "{\"test_obj\":{\"ARGS\": [\"Hello\", \"World\"]}}";
	char *strings[] = {"Hello", "World"};
	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_STRING_ARRAY, "ARGS", {.string_array.count = 2, .string_array.strings = strings}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json19_2(void) {
	char json[] = "{\"test_obj\":{\"ARGS\": []}}";

	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_STRING_ARRAY, "ARGS", {.string_array.count = 0, .string_array.strings = NULL}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json20(void) {
	char json[] = "{\"test_obj\":{\"ARGS\": [\"Hello\", \"\\\"W\\to\\tr\\tl\\td\\n\"]}}";
	char *strings[] = {"Hello", "\"W\to\tr\tl\td\n"};
	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_STRING_ARRAY, "ARGS", {.string_array.count = 2, .string_array.strings = strings}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}


int test_json21(void) {
	char json[] = "{\"test_obj\":{\"TAGS\": {\"Key1\": \"Value 1\", \"Key2\":\"Value 2\"}}}";
	key_val_t map[] = {
		{"Key1", "Value 1"},
		{"Key2", "Value 2"},
	};

	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_MAP, "TAGS", {.map.count = 2, .map.maps = map}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}


int test_json22(void) {
	char json[] = "{\"test_obj\":{\"TAGS\": {}}}";

	struct json_test _test[] = {
		{JSON_OBJ},
		{JSON_NAMED_OBJ, "test_obj"},
		{JSON_MAP, "TAGS", {.map.count = 0, .map.maps = NULL}},
		{JSON_END_OBJ},
		{JSON_END}
	};

	return _test_json(_test, json);
}

int test_json_malformed_1(void) {
	char json[] = "\"test_obj\":{}"; /* Missing surrounding {} */
	char *ptr = json;

	char *obj = JSONGetObject(&ptr);

	if (obj != NULL) {
		printf("Expected not to parse malformed JSON object, but got success.\n");
		printf("Obj pointer is: '%s'\n", obj);
		return 1;
	}

	return 0;
}

int test_json_malformed_2(void) {
	char json[] = "{test_obj\":{}}"; /* Missing quote */
	char *ptr = json;

	char *obj = JSONGetObject(&ptr);

	if (obj != NULL) {
		printf("Expected not to parse malformed JSON object, but got success.\n");
		printf("Obj pointer is: '%s'\n", obj);
		return 1;
	}

	return 0;
}

int test_json_malformed_3(void) {
	char json[] = "{test_obj:{}}"; /* Missing quotes */
	char *ptr = json;

	/* Object should load */
	char *obj = JSONGetObject(&ptr);

	if (obj == NULL) {
		printf("Expected to load object\n");
		return 1;
	}

	/* Should fail when getting the name */
	char *name = JSONGetName(&obj);

	if (name != NULL) {
		printf("Expected not to parse malformed name, but got success.\n");
		printf("Name pointer: '%s'\n", name);
		return 1;
	}

	return 0;
}

int test_json_malformed_4(void) {
	char json[] = "{\"test_obj\":{JOBNAME : \"Hello World\"}}"; /* Missing quotes around name */
	char *ptr = json;

	/* Object should load */
	char *obj = JSONGetObject(&ptr);

	if (obj == NULL) {
		printf("Expected to load object\n");
		return 1;
	}

	/* Should fail when getting the name */
	char *name = JSONGetName(&obj);

	if (name == NULL) {
		printf("Expected get name.\n");
		return 1;
	}

	char * next_obj = JSONGetObject(&obj);

	if (next_obj == NULL) {
		printf("Failed to extract object\n");
		return 1;
	}

	/* Should failed to get this name */
	name = JSONGetName(&next_obj);

	if (name != NULL) {
		printf("Expected to not extract name, but got: '%s'\n", name);
		return 1;
	}

	return 0;
}

int test_json_malformed_5(void) {
	char json[] = "{\"test_obj\":{\"JOBNAME\" : Hello World}}"; /* Missing quotes around value */
	char *ptr = json;

	/* Object should load */
	char *obj = JSONGetObject(&ptr);

	if (obj == NULL) {
		printf("Expected to load object\n");
		return 1;
	}

	char *name = JSONGetName(&obj);

	if (name == NULL) {
		printf("Expected get name.\n");
		return 1;
	}

	char * next_obj = JSONGetObject(&obj);

	if (next_obj == NULL) {
		printf("Failed to extract object\n");
		return 1;
	}

	name = JSONGetName(&next_obj);

	if (name == NULL) {
		printf("Failed to extract the name\n");
		return 1;
	}

	char *value = NULL;
	if (JSONGetString(&next_obj, &value) == 0) {
		printf("Expected to not load a string value, but got: %s\n", value ? value : "NULL");
		return 1;
	}

	return 0;
}

int test_json_malformed_6(void) {
	char json[] = "{\"test_obj\":{\"JOBID\" : 123A5}}"; /* Invalid numeric value */
	char *ptr = json;

	/* Object should load */
	char *obj = JSONGetObject(&ptr);

	if (obj == NULL) {
		printf("Expected to load object\n");
		return 1;
	}

	char *name = JSONGetName(&obj);

	if (name == NULL) {
		printf("Expected get name.\n");
		return 1;
	}

	char * next_obj = JSONGetObject(&obj);

	if (next_obj == NULL) {
		printf("Failed to extract object\n");
		return 1;
	}

	name = JSONGetName(&next_obj);

	if (name == NULL) {
		printf("Failed to extract the name\n");
		return 1;
	}

	int64_t value;
	if (JSONGetNum(&next_obj, &value) == 0) {
		printf("Expected to not load a numeric value, but got: %ld\n", value);
		return 1;
	}

	return 0;
}

int test_json_malformed_7(void) {
	char json[] = "{\"test_obj\":{\"ARGS\":[\"Hello\",\"World\"}}"; /* Unterminated array */
	char *ptr = json;

	/* Object should load */
	char *obj = JSONGetObject(&ptr);

	if (obj == NULL) {
		printf("Expected to load object\n");
		return 1;
	}

	char *name = JSONGetName(&obj);

	if (name == NULL) {
		printf("Expected get name.\n");
		return 1;
	}

	char * next_obj = JSONGetObject(&obj);

	if (next_obj == NULL) {
		printf("Failed to extract object\n");
		return 1;
	}

	name = JSONGetName(&next_obj);

	if (name == NULL) {
		printf("Failed to extract the name\n");
		return 1;
	}

	int64_t count;
	char **array;

	if ((count = JSONGetStringArray(&next_obj, &array)) != -1) {
		printf("Expected to not load the string array, but got %ld items\n", count);
		for (int i  = 0; i < count; i++)
			printf("[%d] %s\n", i, array[i]);

		return 1;
	}

	return 0;
}

/* Message loading tests */

/* Load a error message */
int test_msg_1(void) {
	msg_t loaded = {0};
	msg_t expected = {0};
	char json[] = "{\"ERROR\":\"Error message here\"}";

	/* Setup the expected message */
	expected.msg_cpy = strdup(json);
	expected.error = strdup("Error message here");

	if (load_message(json, &loaded) != 0) {
		//fail.
		return 1;
	}

	if (loaded.msg_cpy == NULL || strcmp(loaded.msg_cpy, expected.msg_cpy) != 0) {
		//fail
		return 1;
	}

	if (loaded.error == NULL || strcmp(loaded.error, expected.error) != 0) {
		//fail
		return 1;
	}

	free_message(&loaded);
	free_message(&expected);

	return 0;
}

/* Load a error message */
int test_msg_2(void) {
	msg_t loaded = {0};
	msg_t expected = {0};
	char json[] = "{\"ERROR\"   :   \"Error\\tmessage here\"}";

	/* Setup the expected message */
	expected.msg_cpy = strdup(json);
	expected.error = strdup("Error\tmessage here");

	if (load_message(json, &loaded) != 0) {
		//fail.
		return 1;
	}

	if (loaded.msg_cpy == NULL || strcmp(loaded.msg_cpy, expected.msg_cpy) != 0) {
		//fail
		return 1;
	}

	if (loaded.error == NULL || strcmp(loaded.error, expected.error) != 0) {
		//fail
		return 1;
	}

	free_message(&loaded);
	free_message(&expected);

	return 0;
}

int test_msg_returncode_1(void) {
	char json[] = "{\"RESP\":{\"RETURN_CODE\":\"0\"}}";
	msg_t loaded = {0};
	msg_t expected = {0};

	expected.command = "0";

	if (load_message(json, &loaded) != 0) {
		printf("Failed to load message\n");
		return 1;
	}

	if (loaded.command == NULL || strcmp(loaded.command, expected.command) != 0) {
		printf("ReturnCode not populated or not expected value. Got:'%s' Expected:'%s'\n", loaded.command? loaded.command : "NULL", expected.command);
		return 1;
	}

	free_message(&loaded);
	free_message(&expected);

	return 0;
}

int test_msg_returncode_2(void) {
	char json[] = "{\"RESP\":{\"RETURN_CODE\":\"1\"}}";
	msg_t loaded = {0};
	msg_t expected = {0};

	expected.command = "1";

	if (load_message(json, &loaded) != 0) {
		printf("Failed to load message\n");
		return 1;
	}

	if (loaded.command == NULL || strcmp(loaded.command, expected.command) != 0) {
		printf("ReturnCode not populated or not expected value. Got:'%s' Expected:'%s'\n", loaded.command? loaded.command : "NULL", expected.command);
		return 1;
	}

	free_message(&loaded);
	free_message(&expected);

	return 0;
}

void test_json(void) {

	/* Test we can construct a simple JSON object with a few different fields */
	TEST("Create empty JSON object", test_json1());
	TEST("Create single JSON object", test_json2());
	TEST("Create two JSON objects", test_json3());
	TEST("Create JSON object with single field", test_json4());
	TEST("Create JSON object with array", test_json5());
	TEST("Create JSON object with empty array", test_json5_2());
	TEST("Create JSON object with multiple fields", test_json6());
	TEST("Create JSON object with string field requiring escaping", test_json7());
	TEST("Create JSON object with string field requiring lots of escaping", test_json8());
	TEST("Create JSON object with 'map' field", test_json9());
	TEST("Create JSON object with empty 'map' field", test_json9_2());
	TEST("Create JSON object with single fixed length string field", test_json10());
	TEST("Create JSON object with null string field", test_json11());
	TEST("Create JSON object with bool field", test_json12());

	TEST("Load single JSON object", test_json13());
	TEST("Load JSON object with single string field", test_json14());
	TEST("Load JSON object with single string field, unescaping required", test_json15());
	TEST("Load JSON object with number field", test_json16());
	TEST("Load JSON object with null string", test_json17());
	TEST("Load JSON object with bool fields", test_json18());
	TEST("Load JSON object with string array", test_json19());
	TEST("Load JSON object with empty string array", test_json19_2());
	TEST("Load JSON object with string array escaping required", test_json20());
	TEST("Load JSON object with map field", test_json21());
	TEST("Load JSON object with empty map field", test_json22());

	TEST("Load malformed JSON, no {}", test_json_malformed_1());
	TEST("Load malformed JSON, missing quote", test_json_malformed_2());
	TEST("Load malformed JSON, missing quotes", test_json_malformed_3());
	TEST("Load malformed JSON, missing quotes from name", test_json_malformed_4());
	TEST("Load malformed JSON, missing quotes from value", test_json_malformed_5());
	TEST("Load malformed JSON, invalid numeric value", test_json_malformed_6());
	TEST("Load malformed JSON, unterminated array", test_json_malformed_7());

	TEST("Load Error message", test_msg_1());
	TEST("Load Error message, unescaping", test_msg_2());
	TEST("Load returncode response '0'", test_msg_returncode_1());
	TEST("Load returncode response '1'", test_msg_returncode_2());

}
