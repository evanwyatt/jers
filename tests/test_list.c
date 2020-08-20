#include <stdio.h>

#include <jers_tests.h>
#include <common.h>

static int cmp_list(const void *a, const void *b, void *arg) {
	(void) arg;
	return *(int *)b - *(int *)a;
}

int list_test_insert(void) {
	struct item_list list;

	listNew(&list, sizeof(int));

	for (int i = 0; i < 1000; i++)
		listAdd(&list, &i);

	int check = 0;
	int *item = NULL;

	LIST_ITER(&list, item) {
		if (*item != check) {
			fprintf(stderr, "List insert check failed, got: %d, expected: %d\n", *item, check);
			return 1;
		}

		check++;	
	}

	/* Sort it backwards and check again */
	listSort(&list, cmp_list, NULL);
	check = 999;

	LIST_ITER(&list, item) {
		if (*item != check) {
			fprintf(stderr, "List check after sort failed, got: %d, expected: %d\n", *item, check);
			return 1;
		}
		check--;
	}

	return 0;
}

void test_list(void) {
	TEST("addItem - Inserting", list_test_insert());
}