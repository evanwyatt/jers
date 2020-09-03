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

int check_matches_flag(const char *pattern, const char *string, int flag, int expect_match) {
	/* matches() returns 0 - if the pattern matches, < 0 or 0 > if it doesn't match */
	int match = 0;

	if (matches_wildcard(pattern, string, flag) == 0)
		match = 1;

	if (match != expect_match) {
		if (__debug) {
			printf("Unexpected result for wildcard match. Pattern:'%s' String:'%s' Flag:%s\n", pattern, string, flag? "True":"False");
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

int check_getArg(char *_string, char **expected_array) {
	int i = 0;
	char *arg = NULL;
	char *copy = _string ? strdup(_string) : NULL;
	char *string = copy;

	while ((arg = getArg(&string)) != NULL) {
		if (expected_array[i] == NULL) {
			if (__debug)
				printf("Unexpected result for getArg. Expected NULL, but got a result back '%s'\n", arg);

			free(copy);
			return 1;
		}

		if (strcmp(arg, expected_array[i]) != 0) {
			if (__debug)
				printf("Unexpected result for stringtoarray. Expected:'%s' Got:'%s'\n", expected_array[i], arg);

			free(copy);
			return 1;
		}

		i++;
	}

	if (expected_array && expected_array[i] != NULL) {
		if (__debug)
			printf("Unexpected result for getArg. Expected a result of '%s', but got NULL\n", expected_array[i]);

		free(copy);
		return 1;
	}

	free(copy);
	return 0;
}


int check_seperateTokens(char *_string, char sep, char **expected_array) {
	char *copy = _string ? strdup(_string) : NULL;
	int rc = 0;
	int count = 0;

	char **result = seperateTokens(copy, sep);

	if (result == NULL) {
		if (__debug)
			printf("Unexpected NULL result from seperateTokens\n");

		rc = 1;
		goto seperateTokens_done;
	}

	for (count = 0; result[count] && expected_array[count]; count++) {
		if (strcmp(result[count], expected_array[count]) != 0) {
			if (__debug)
				printf("Unexpected result from seperateTokens. Got: '%s' Expected: '%s'\n", result[count], expected_array[count]);

			rc = 1;
			goto seperateTokens_done;
		}
	}

	if (result[count] || expected_array[count]) {
		if (__debug)
			printf("Unexpected result from seperateTokens. Got: '%s' Expected: '%s'\n", result[count], expected_array[count]);

		rc = 1;
		goto seperateTokens_done;
	}

seperateTokens_done:
	free(copy);
	free(result);
	return rc;
}

struct keyvalue_test {
	char *key;
	char *value;
	int index;
};

int test_loadKeyValue(char *input, struct keyvalue_test *expected)
{
	int rc = 0;
	struct keyvalue_test result = {0};
	char *_input = strdup(input);

	loadKeyValue(_input, &result.key, &result.value, &result.index);
	if (expected->key == NULL || result.key == NULL) {
		if (result.key != expected->key) {
			rc = 1;
			goto test_loadKeyValue_finished;
		}
	} else if (strcmp(expected->key, result.key) != 0) {
		rc = 1;
		goto test_loadKeyValue_finished;
	}

	if (expected->value == NULL || result.value == NULL) {
		if (result.value != expected->value) {
			rc = 1;
			goto test_loadKeyValue_finished;
		}
	} else if (strcmp(expected->key, result.key) != 0) {
		rc = 1;
		goto test_loadKeyValue_finished;
	}

	if (expected->index != result.index) {
		rc = 1;
		goto test_loadKeyValue_finished;
	}

test_loadKeyValue_finished:
	if (rc) {
		printf("Unexpected result from test_loadKeyValue: '%s'\n", input);
		printf("Expected Key = '%s'\n", expected->key ? expected->key : "(NULL)");
		printf("Result Key   = '%s'\n", result.key ? result.key : "(NULL)");
		printf("Expected value = '%s'\n", expected->value ? expected->value : "(NULL)");
		printf("Result value   = '%s'\n", result.value ? result.value : "(NULL)");
		printf("Expected index = '%d'\n", expected->index);
		printf("Result index   = '%d'\n", result.index);
	}
	
	free(_input);
	return rc;
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
	TEST("removeWhitespace - No removal required", check_whitespace("Hello \tWorld", "Hello \tWorld"));

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
	TEST("matches - Non matching string", check_matches("Hello", "Hello World", 0));
	TEST("matches - Non matching string, wildcard", check_matches("Hello*", "World", 0));

	/* matches - wildcard flag */

	TEST("matches (flag = n) - empty strings", check_matches_flag("", "", 0, 1));
	TEST("matches (flag = y) - empty strings", check_matches_flag("", "", 1, 1));

	TEST("matches - empty string (flag = n), wilcard", check_matches_flag("*", "", 0, 0));
	TEST("matches - empty string (flag = y), wilcard", check_matches_flag("*", "", 1, 1));

	TEST("matches - literal '*' (flag = n), wilcard", check_matches_flag("*", "Hello World", 0, 0));
	TEST("matches - literal '*' (flag = y), wilcard", check_matches_flag("*", "Hello World", 1, 1));

	TEST("matches - '*' string (flag = n), wilcard", check_matches_flag("*", "*", 0, 1));
	TEST("matches - '*' string (flag = y), wilcard", check_matches_flag("*", "*", 1, 1));

	TEST("matches - Wilcard (flag = n)", check_matches_flag("Hello*", "Hello World", 0, 0));
	TEST("matches - Wilcard (flag = y)", check_matches_flag("Hello*", "Hello World", 1, 1));

	TEST("matches - Non matching string (flag = n)", check_matches_flag("Hello", "Hello World", 0, 0));
	TEST("matches - Non matching string, wildcard (flag = n)", check_matches_flag("Hello*", "World", 0, 0));

	TEST("matches - Non matching string (flag = y)", check_matches_flag("Hello", "Hello World", 1, 0));
	TEST("matches - Non matching string, wildcard (flag = y)", check_matches_flag("Hello*", "World", 1, 0));

	/* check_name - check a string is a valid posix portable filename */
	TEST("checkname - Empty string", check_checkname("", 1));
	TEST("checkname - Valid name", check_checkname("helloworld.txt", 1));
	TEST("checkname - Valid name", check_checkname("HelloWorld.txt", 1));
	TEST("checkname - Valid name", check_checkname("hello_world.txt", 1));
	TEST("checkname - Invalid name", check_checkname("/hello/world.txt", 0));
	TEST("checkname - Invalid name", check_checkname("/hello/world.txt", 0));
	TEST("checkname - Invalid name", check_checkname("hello(world).txt", 0));

	TEST("getArg - NULL", check_getArg(NULL, NULL));
	TEST("getArg - Empty string", check_getArg("", NULL));

	char *getArgResult1[] = {"Hello", "World", NULL};
	TEST("getArg - Simple string", check_getArg("Hello World", getArgResult1));
	TEST("getArg - Simple string", check_getArg("Hello\tWorld", getArgResult1));
	TEST("getArg - Simple string, quoted", check_getArg("'Hello' 'World'", getArgResult1));
	TEST("getArg - Simple string, quoted", check_getArg("\"Hello\" 'World'", getArgResult1));

	char *getArgResult2[] = {"H e l l o", "World", NULL};
	TEST("getArg - Simple string, quote with whitespace", check_getArg("\"H e l l o\" World", getArgResult2));

	char *seperateTokensResult1[] = {"Hello World", NULL};
	TEST("seperateTokens - Simple - No sep ", check_seperateTokens("Hello World", ',', seperateTokensResult1));

	char *seperateTokensResult2[] = {"Hello", "World", NULL};
	TEST("seperateTokens - Simple ", check_seperateTokens("Hello,World", ',', seperateTokensResult2));
	TEST("seperateTokens - Simple, whitespace", check_seperateTokens("   Hello  ,   World", ',', seperateTokensResult2));
	TEST("seperateTokens - Pipe delimited, whitespace", check_seperateTokens("   Hello  |   World   ", '|', seperateTokensResult2));

	char *seperateTokensResult3[] = {"Hello", "W o r l d", NULL};
	TEST("seperateTokens - Token with embedded whitespace", check_seperateTokens("Hello,   W o r l d   ", ',', seperateTokensResult3));

	/* LoadKeyValue
	 * Loads strings that looks like:
	 * 'key value'
	 * 'key[1] value */

	struct keyvalue_test result;

	result.key = NULL;
	result.value = NULL;
	result.index = 0;

	TEST("loadKeyValue - Empty string", test_loadKeyValue("", &result));
	TEST("loadKeyValue - \"#key value\"", test_loadKeyValue("#key value", &result));
	TEST("loadKeyValue - \"    #key value\"", test_loadKeyValue("    #key value", &result));
	TEST("loadKeyValue - \" #  key value\"", test_loadKeyValue(" #  key value", &result));

	result.key = "key";
	result.value = "value";
	result.index = 0;
	TEST("loadKeyValue - key value", test_loadKeyValue("key value", &result));
	TEST("loadKeyValue - key value", test_loadKeyValue("  key     value", &result));
	TEST("loadKeyValue - key value", test_loadKeyValue("  key   \t  value     ", &result));
	TEST("loadKeyValue - key value\\n", test_loadKeyValue("key value\n", &result));

	result.key = "key";
	result.value = "value";
	result.index = 1;
	TEST("loadKeyValue - key[index] value", test_loadKeyValue("key[1] value", &result));
	TEST("loadKeyValue - key[index] value", test_loadKeyValue("  key[1] value", &result));
	TEST("loadKeyValue - key[index] value", test_loadKeyValue("\tkey[1]    value", &result));


	result.key = "key";
	result.value = "v\ta\tl\tu\te";
	result.index = 0;
	TEST("loadKeyValue - Escaped value", test_loadKeyValue("key  v\\t\\ta\\t\\tl\\tu\\te", &result));
}