#include <stdio.h>

#include <jers_tests.h>
#include <common.h>

static int check_result(const char *result, const char *expected) {
	if (result == NULL) {
		if (__debug)
			printf("Result from hexEncode was NULL\n");
		return 1;
	} else if (strcmp(expected, result) != 0) {
		if (__debug)
			printf("Unexpected result\nGot   :'%s'\nWanted:'%s'\n", result, expected);
		return 1;
	}

	return 0;
}

void test_hexEncode(void) {
	char *result = NULL;
	char *input = "test input string";
	char *expected_result = "7465737420696E70757420737472696E67";
	size_t input_len = strlen(input);

	result = hexEncode((unsigned char *)input, input_len, NULL);
	TEST("Standard encoding", check_result(result, expected_result));

	free(result);

	/* Test length is used correctly */
	input = "another test string. Extra text";
	input_len = 20;
	expected_result = "616E6F74686572207465737420737472696E672E";

	result = hexEncode((unsigned char *)input, input_len, NULL);
	TEST("Encoding with length", check_result(result, expected_result));

	free(result);

	/* Using a provided buffer */
	char result_buff[64];
	input = "test string";
	input_len = strlen(input);
	expected_result = "7465737420737472696E67";

	result = hexEncode((unsigned char *)input, input_len, result_buff);
	TEST("Encoding into supplied buffer", result_buff == result && check_result(result_buff, expected_result));

	/* INT */
	int i = 128;
	input_len = sizeof(int);
	expected_result = "80000000";

	result = hexEncode((unsigned char *)&i, input_len, NULL);
	TEST("Encoding integer", check_result(result, expected_result));

	free(result);

	i = -442222;
	input_len = sizeof(int);
	expected_result = "9240F9FF";

	result = hexEncode((unsigned char *)&i, input_len, NULL);
	TEST("Encoding negative integer", check_result(result, expected_result));

	free(result);

	return;
}

