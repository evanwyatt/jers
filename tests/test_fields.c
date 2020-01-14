#include <stdio.h>
#include <ctype.h>

#include <jers_tests.h>
#include <fields.h>

extern field fields[];
extern field *sortedFields;
extern int field_count;

int check_names(void) {
	char upper[1024];

	/* Check all the field names are uppercase and the length matches */
	for (int i = 0; i < field_count; i++) {
		int j;
		for (j = 0; fields[i].name[j]; j++)
			upper[j] = toupper(fields[i].name[j]);

		upper[j] = 0;

		if (strcmp(upper, fields[i].name) != 0) {
			if (__debug)
				printf("Fieldname '%s' is not uppercase\n", fields[i].name);
			return 1;
		}

		if (strlen(fields[i].name) != fields[i].name_len) {
			if (__debug)
				printf("Field '%s' name len is not correct. Expected: %ld Got:%ld\n",
					fields[i].name, fields[i].name_len, strlen(fields[i].name));
			return 1;
		}

		/* Check the field number matches with the array entry */
		if (fields[i].number != ENDOFFIELDS && fields[fields[i].number].number != fields[i].number) {
			if (__debug)
				printf("Field '%s' number not correct\n", fields[i].name);
			return 1;
		}
	}

	return 0;
}

int check_sort() {
	/* Sort the fields and check they are in order */
	sortfields();
	for (int i = 0; i < field_count - 1; i++) {
		if (strcmp(sortedFields[i].name, sortedFields[i + 1].name) >= 0) {
			if (__debug)
				printf("Fields are not in sorted order.");

			return 1;
		}
	}

	return 0;
}

void test_fields(void) {
	TEST("Field names", check_names());
	TEST("Sorted fields", check_sort());
}