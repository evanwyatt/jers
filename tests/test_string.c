#include <stdio.h>

#include <jers_tests.h>
#include <common.h>

int check_whitespace(const char *input, const char *expected) {
	char b[4096];
	strcpy(b, input);

	char * result = removeWhitespace(b);

	/* Result should be a pointer to our buffer */
	if (result == NULL) {
		if (__debug)
			printf("Result is NULL\n");
		return 1;
	}

	if (result != b) {
		if (__debug)
			printf("Result is not a pointer to our buffer\n");
		return 1;
	}

	if (strcmp(result, expected) != 0) {
		if (__debug)
			printf("Result does not match expected. Got: '%s' Expected '%s'\n", result, expected);
		return 1;
	}

	return 0;
}

int check_skipChars(char *input, const char *skip, const char *expected) {
	char *ptr = skipChars(input, skip);

	if (ptr == NULL) {
		if (__debug)
			printf("Return pointer is NULL\n");
		return 1;
	}

	if (ptr < input) {
		if (__debug)
			printf("Returned pointer is before the input string\n");
		return 1;
	}

	if (ptr > input + strlen(input)) {
		if (__debug)
			printf("Returned pointer is past our input string\n");
		return 1;
	}

	if (strcmp(ptr, expected) != 0) {
		if (__debug)
			printf("Returned pointer does not match expected result\n");
		return 1;
	}

	return 0;
}

int check_skipWhitespace(char *input, const char *expected) {
	char *ptr = skipWhitespace(input);

	if (ptr == NULL) {
		if (__debug)
			printf("Return pointer is NULL\n");
		return 1;
	}

	if (ptr < input) {
		if (__debug)
			printf("Returned pointer is before the input string\n");
		return 1;
	}

	if (ptr > input + strlen(input)) {
		if (__debug)
			printf("Returned pointer is past our input string\n");
		return 1;
	}

	if (strcmp(ptr, expected) != 0) {
		if (__debug)
			printf("Returned pointer does not match expected result\n");
		return 1;
	}

	return 0;
}

int check_uppercase(const char *input, const char *expected) {
	char _input[4096];
	strcpy(_input, input);

	uppercasestring(_input);

	if (strcmp(_input, expected) != 0) {
		if (__debug)
			printf("uppercase string does not match. Got:'%s' Expected: '%s'\n", input, expected);

		return 1;
	}

	return 0;
}

int check_lowercase(const char *input, const char *expected) {
	char _input[4096];
	strcpy(_input, input);

	lowercasestring(_input);

	if (strcmp(_input, expected) != 0) {
		if (__debug)
			printf("lowercase string does not match. Got:'%s' Expected: '%s'\n", input, expected);

		return 1;
	}

	return 0;
}

int check_int64tostr(int64_t num) {
	char ours[32];
	char expected[32];

	int expected_len = sprintf(expected, "%ld", num);
	int our_len = int64tostr(ours, num);

	if (expected_len != our_len) {
		if (__debug)
			printf("Length of string wrong. Got: %d Expected: %d\n", our_len, expected_len);
		return 1;
	}

	if (strcmp(ours, expected) != 0) {
	   if (__debug)
			printf("Strings don't match. Got:'%s' Expected:'%s'\n", ours, expected);
		return 1;
	}

	return 0;
}

int check_matches(const char *pattern, const char *string, int expect_match) {
	/* matches() returns 0 - if the pattern matches, < 0 or 0 > if it doesn't match */
	int match = 0;

	if (matches(pattern, string) == 0)
		match = 1;

	if (match != expect_match) {
		if (__debug) {
			printf("Unexpected result for wildcard match. Pattern:'%s' String:'%s'\n", pattern, string);
			printf("Match:%d Expected:%d\n", match, expect_match);
		}
		return 1;
	}

	return 0;
}

int check_checkname(char *str, int valid) {
	int match = 0;

	if (check_name(str) == 0)
		match = 1;

	if (valid != match) {
		if (__debug) {
			printf("Unexpected result for checkname(). String: '%s'\n", str);
			printf("Match:%d Expected:%d\n", match, valid);
		}
		return 1;
	}

	return 0;
}

void test_strings(void) {
	/* removeWhitespace - Expect leading and trailing whitespace
	 * (Spaces and tabs) to be removed */
	TEST("removeWhitespace - Empty string", check_whitespace("", ""));
	TEST("removeWhitespace - Leading tab", check_whitespace("\tTesting string", "Testing string"));
	TEST("removeWhitespace - Leading spaces", check_whitespace("	   Testing string", "Testing string"));
	TEST("removeWhitespace - Leading spaces with embedded tabs", check_whitespace("	   Testing\tstring", "Testing\tstring"));
	TEST("removeWhitespace - Leading and trailing spaces", check_whitespace("  Testing string	", "Testing string"));
	TEST("removeWhitespace - Leading and trailing tabs", check_whitespace("\t\tTesting\tstring\t\t", "Testing\tstring"));

	/* skipChars - Return a pointer inside your string to the first non-matching char */
	TEST("skipChars - no skip", check_skipChars("Hello World", "", "Hello World"));
	TEST("skipChars - skip whitespace", check_skipChars(" \t Hello World", " \t", "Hello World"));
	TEST("skipChars - skip numeric", check_skipChars("123Hello World12345", "0123456789", "Hello World12345"));

	/* skipWhitespace - Returns a pointer inside the string to the first non-whitespace (Space or tab) */
	TEST("skipWhitesapce - no skip", check_skipWhitespace("Hello World", "Hello World"));
	TEST("skipWhitesapce - leading whitespace", check_skipWhitespace("	   Hello World", "Hello World"));
	TEST("skipWhitesapce - leading whitespace, with trailing", check_skipWhitespace("	   Hello World	 ", "Hello World	 "));
	TEST("skipWhitesapce - leading whitespace, with trailing tab", check_skipWhitespace("\t \t \tHello World\t", "Hello World\t"));

	/* uppercasestring */

	TEST("uppercasestring - empty string", check_uppercase("", ""));
	TEST("uppercasestring", check_uppercase("hello world", "HELLO WORLD"));
	TEST("uppercasestring", check_uppercase("HELLO WORLD", "HELLO WORLD"));

	/* lowercasestring */
	TEST("lowercasestring - empty string", check_lowercase("", ""));
	TEST("lowercasestring - lowercase string", check_lowercase("hello world", "hello world"));
	TEST("lowercasestring - already uppercase", check_lowercase("HELLO WORLD", "hello world"));

	/* int64tostr - Compare our implemntation with sprintf */
	TEST("int64tostr - 0", check_int64tostr(0));
	TEST("int64tostr - -1", check_int64tostr(-1));
	TEST("int64tostr - 1", check_int64tostr(1));
	TEST("int64tostr - INT64_MAX", check_int64tostr(INT64_MAX));
	TEST("int64tostr - INT64_MIN", check_int64tostr(INT64_MIN));

	/* matches - wilcard matching */
	TEST("matches - empty strings", check_matches("", "", 1));
	TEST("matches - empty string, wilcard", check_matches("*", "", 1));
	TEST("matches - Wilcard", check_matches("Hello*", "Hello World", 1));
	TEST("matches - Single character wildcard", check_matches("Hello?World", "Hello World", 1));
	TEST("matches - Non matching wildcard", check_matches("World*", "Hello World", 0));
	TEST("matches - Multiple wildcard charaters", check_matches("?ello W*", "Hello World", 1));

	/* check_name - check a string is a valid posix portable filename */
	TEST("checkname - Empty string", check_checkname("", 1));
	TEST("checkname - Valid name", check_checkname("helloworld.txt", 1));
	TEST("checkname - Valid name", check_checkname("HelloWorld.txt", 1));
	TEST("checkname - Valid name", check_checkname("hello_world.txt", 1));
	TEST("checkname - Invalid name", check_checkname("/hello/world.txt", 0));
	TEST("checkname - Invalid name", check_checkname("/hello/world.txt", 0));
	TEST("checkname - Invalid name", check_checkname("hello(world).txt", 0));

}