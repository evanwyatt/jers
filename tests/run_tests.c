#include <stdio.h>
#include <stdlib.h>

#include <jers_tests.h>
#include <server.h>

#define UNUSED(x) (void)(x)
char sub_test[4096] = "";
int __status = 0;
int __debug = 0;

/* Couple of fake variable to trick routines into thinking we are jersd */
char * server_log = "jersd";
int server_log_mode = JERS_LOG_CRITICAL;

void test_hexEncode(void);
void test_escaping(void);
void test_time(void);
void test_strings(void);
void test_fields(void);
void test_buffers(void);
void test_json(void);
void test_jobs(void);

struct test_case {
	const char *name;
	void (*func)(void);
};

struct test_case test_cases[] = {
	{"Hex encoding", test_hexEncode},
	{"String escaping", test_escaping},
	{"Time functions", test_time},
	{"String functions", test_strings},
	{"Fields", test_fields},
	{"Buffers", test_buffers},
	{"JSON", test_json},
	{"Jobs", test_jobs},
};

int main (int argc, char *argv[]) {
	UNUSED(argc);
	UNUSED(argv);
	int failed_count = 0;
	int test_count = 0;
	int number_tests = sizeof(test_cases) / sizeof(struct test_case);

	char *debug = getenv("JERS_TEST_DEBUG");
	if (debug && *debug == 'Y') {
		__debug = 1;
		server_log_mode = JERS_LOG_DEBUG;
	}

	printf("Running tests.\n");

	for (int i = 0; i < number_tests; i++) {
		printf("[%02d/%02d] %s... \n", i + 1, number_tests, test_cases[i].name);
		__status = 0;

		test_cases[i].func();

		if (__status)
			failed_count++;

		test_count++;
	}

	printf("\n====================================\n"
			 "%d tests, %d passed, %d failed\n\n", test_count, test_count - failed_count, failed_count);

	return failed_count ? 1 : 0;
}