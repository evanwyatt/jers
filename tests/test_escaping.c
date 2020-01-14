#include <stdio.h>

#include <jers_tests.h>
#include <common.h>

static int check(const char *input, const char *expected, int length) {
	size_t _len = 0;
	char *result = escapeString(input, length ? &_len : NULL);

	if (result == NULL) {
		if (__debug)
			printf("result is NULL\n");
		return 1;
	}

	if (length && _len != strlen(result)) {
		if (__debug)
			printf("length is incorrect, got:%ld expected%ld\n", _len, strlen(result));
		return 1;
	}

	if (strcmp(result, expected) != 0) {
		if (__debug)
			printf("Result is incorrect. Got: '%s' Expected: '%s'\n", result, expected);
		return 1;
	}

	/* Check we get the original string when unescaping */
	unescapeString(result);

	if (strcmp(input, result) != 0) {
		if (__debug)
			printf("Unescaped result does not match input. Got: '%s' Expected '%s'\n", result, input);

		return 1;
	}

	return 0;
}

/* Test escaping of newlines and tab character */
void test_escaping(void) {
	TEST("No escaping required, no length", check("Testing escaped string", "Testing escaped string", 0));
	TEST("No escaping required, length", check("Testing escaped string", "Testing escaped string", 1));
	TEST("Escaping tabs", check("string\twith\ttabs\t", "string\\twith\\ttabs\\t", 1));
	TEST("Single tab", check("\t", "\\t", 1));
	TEST("Empty string", check("", "", 1));
	TEST("Slash and tab", check("\\slash\t", "\\\\slash\\t", 1));
	TEST("Trailing newline", check("string\n", "string\\n", 1));
	TEST("Multiple newline", check("string\n\n\n\n", "string\\n\\n\\n\\n", 1));
	TEST("Slashes", check("\\slash\\", "\\\\slash\\\\", 1));
}