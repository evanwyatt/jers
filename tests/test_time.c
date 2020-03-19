#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include <jers_tests.h>
#include <common.h>
/* Bit dodgy, but allow the times to be 5 milliseconds either side due to CLOCK_MONOTONIC_COARSE resolution */
int check_duration (int64_t start, int64_t finish, int64_t expected) {
	int64_t duration = finish - start;
	int fudge_factor = 5;

	if (finish <= start) {
		if (__debug)
			printf("Finish time < start time\n");
		return 1;
	}

	if (duration < expected) {
		if (expected % duration > fudge_factor) {
			if (__debug)
				printf("Duration < expected. Duration: %ldms Expected: %ldms\n", duration, expected);
			return 1;
		}
	} else if (duration % expected > fudge_factor) {
		if (__debug)
			printf("Duration > expected. Duration: %ldms Expected: %ldms\n", duration, expected);
		return 1;
	}

	return 0;
}

int check_timestring(const struct timespec *input, int elapsed, const char *expected) {
	char *result = print_time(input, elapsed);

	if (strcmp(result, expected) != 0) {
		if (__debug)
			printf("Expected: '%s' Got: '%s'\n", expected, result);
		return 1;
	}

	return 0;
}

int check_diff(struct timespec *start, struct timespec *finish, struct timespec *expected) {
	struct timespec diff;
	timespec_diff(start, finish, &diff);

	if (diff.tv_sec != expected->tv_sec) {
		if (__debug)
			printf("Diff seconds different, expected %ld, got %ld\n", expected->tv_sec, diff.tv_sec);
		return 1;
	}

	if (diff.tv_nsec != expected->tv_nsec) {
		if (__debug)
			printf("Diff nanoseconds different, expected %ld, got %ld\n", expected->tv_nsec, diff.tv_nsec);
		return 1;
	}

	return 0;
}

void test_time(void) {
	/* Bit dodgy, but we expect this to be within 1% */
	int64_t start = getTimeMS();
	sleep(1);
	int64_t finish = getTimeMS();

	TEST("Duration test - 1 second", check_duration(start, finish, 1000));

	struct timespec s = {.tv_sec = 0, .tv_nsec = 50000000};
	start = getTimeMS();
	nanosleep(&s, NULL);
	finish = getTimeMS();

	TEST("Duration test - .05 second", check_duration(start, finish, 50));

	/* Test printing of times */
	//char * print_time(const struct timespec * time, int elapsed);
	struct timespec input;
	input.tv_sec = 1;
	input.tv_nsec = 0;

	TEST("Elapsed format 1 second", check_timestring(&input, 1, "0m 1.000s"));

	input.tv_sec = 0;
	input.tv_nsec = 500000000;

	TEST("Elapsed format 1", check_timestring(&input, 1, "0m 0.500s"));

	input.tv_sec = 10 * 60;
	input.tv_nsec = 950000000;

	TEST("Elapsed format 2", check_timestring(&input, 1, "10m 0.950s"));

	input.tv_sec = (5 * 60 * 60) + (15 * 60);
	input.tv_nsec = 50000000;

	TEST("Elapsed format 3", check_timestring(&input, 1, "5h 15m 0.050s"));

	input.tv_sec = (1 * 60 * 60) + (59 * 60) + 59;
	input.tv_nsec = 999999999;

	TEST("Elapsed format 4", check_timestring(&input, 1, "1h 59m 59.999s"));

	/* Time stamps are in localtime, so need to figure out a way to make this reliable */
	input.tv_sec = 1570000000;
	input.tv_nsec = 500000000;
	TEST("Timestamp", check_timestring(&input, 0, "2019-10-02 17:06:40.500 AEST"));

	input.tv_sec = 1590001683;
	input.tv_nsec = 54000000;
	TEST("Timestamp 2", check_timestring(&input, 0, "2020-05-21 05:08:03.054 AEST"));

	/* Timespec differences */
	struct timespec diff_start, diff_finish, expected_diff;

	diff_start.tv_sec = 1577863800;
	diff_start.tv_nsec = 0;
	diff_finish.tv_sec = 1577867400;
	diff_finish.tv_nsec = 0;
	expected_diff.tv_sec = 1 * 60 * 60; // One hour
	expected_diff.tv_nsec = 0;

	TEST("Timespec difference 1", check_diff(&diff_start, &diff_finish, &expected_diff));

	diff_start.tv_sec = 1577863800;
	diff_start.tv_nsec = 0;
	diff_finish.tv_sec = 1577863800;
	diff_finish.tv_nsec = 500000000;
	expected_diff.tv_sec = 0;
	expected_diff.tv_nsec = 500000000;

	TEST("Timespec difference 2", check_diff(&diff_start, &diff_finish, &expected_diff));
}