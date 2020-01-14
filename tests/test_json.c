#include <stdio.h>

#include <jers_tests.h>
#include <json.h>

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

void test_json(void) {

	/* Test we can construct a simple JSON object with a few different fields */
	TEST("Create empty JSON object", test_json1());
	TEST("Create single JSON object", test_json2());
	TEST("Create two JSON objects", test_json3());
	TEST("Create JSON object with single field", test_json4());
	TEST("Create JSON object with array", test_json5());
	TEST("Create JSON object with multiple fields", test_json6());
	TEST("Create JSON object with string field requiring escaping", test_json7());
	TEST("Create JSON object with string field requiring lots of escaping", test_json8());
	TEST("Create JSON object with 'map' field", test_json9());
	TEST("Create JSON object with single fixed length string field", test_json10());
	TEST("Create JSON object with null string field", test_json11());
	TEST("Create JSON object with bool field", test_json12());

}