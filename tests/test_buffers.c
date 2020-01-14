#include <stdio.h>
#include <string.h>

#include <jers_tests.h>
#include <buffer.h>

/* Check if a buffer has been free'd correctly */
int check_free(buff_t *buff) {
	if (buff->data != NULL) {
		DEBUG("free check: data != NULL");
		return 1;
	}

	if (buff->size != 0) {
		DEBUG("free check: size != 0");
		return 1;
	}

	if (buff->used != 0) {
		DEBUG("free check: used != 0");
		return 1;
	}

	return 0;
}

int check_buffer(buff_t *buff, buff_t *expected) {
	/* Content length */
	if (buff->used != expected->used) {
		DEBUG("Buffer used length does not match. Expected: %ld Got: %ld\n", expected->used, buff->used);
		goto failed;
	}

	/* Make sure the size is > than expected */
	if (buff->size < expected->size) {
		DEBUG("Buffer size does not match. Expected: %ld Got: %ld\n", expected->size, buff->size);
		goto failed;
	}

	if (buff->data == NULL) {
		DEBUG("Buffer data is NULL\n");
		goto failed;
	}

	if (memcmp(buff->data, expected->data, buff->used) != 0) {
		DEBUG("Buffer data is different. Expected: '%s' Got: '%s'\n", expected->data, buff->data);
		goto failed;
	}

	buffFree(buff);
	return check_free(buff);

failed:
	buffFree(buff);
	return 1;
}

/* Check the buffer can store at least an extra 'size' worth of bytes */
int check_resize(buff_t *buff, size_t size) {
	if (buff->size - buff->used <= size) {
		DEBUG("check_resize: Expected to store %ld bytes. Can only store: %ld\n", size, buff->size - buff->used);
		buffFree(buff);
		return 1;
	}

	buffFree(buff);
	return check_free(buff);
}

int check_clear(buff_t *buff, size_t resize) {
	if (buff->used != 0) {
		DEBUG("check_clear: Used != 0 Used:%ld\n", buff->used);
		buffFree(buff);
		return 1;
	}

	if (resize) {
		if (buff->size != resize) {
			DEBUG("check_clear: Resize specified, but does not match buffer. Expected: %ld Got: %ld\n", resize, buff->size);
			buffFree(buff);
			return 1;
		}
	}

	buffFree(buff);
	return check_free(buff);
}

/* test the buffer routines. - Each test also implicitly checks the buffFree() function() */

void test_buffers(void) {
	buff_t buff = {0};
	buff_t expected;
	char *data;

	buffNew(&buff, 0);
	expected.used = 0;
	expected.size = BUFF_DEFAULT_SIZE;
	expected.data = "";

	TEST("buffNew - No size", check_buffer(&buff, &expected));

	buffNew(&buff, BUFF_DEFAULT_SIZE + 100);
	expected.used = 0;
	expected.size = BUFF_DEFAULT_SIZE + 100;
	expected.data = "";

	TEST("buffNew - Size provided", check_buffer(&buff, &expected));

	/* buffResize */
	buffNew(&buff, 10);
	buffResize(&buff, 1000);

	TEST("buffResize", check_resize(&buff, 1000));


	/* Add to a buffer that hasn't been initalised */
	data = "Test string.";
	buffAdd(&buff, data, strlen(data));
	expected.data = data;
	expected.size = BUFF_DEFAULT_SIZE;
	expected.used = strlen(data);

	TEST("buffAdd - Not initalised", check_buffer(&buff, &expected));

	/* Buff clear */
	data = "string.";
	buffAdd(&buff, data, strlen(data));
	buffClear(&buff, 0);

	TEST("buffClear", check_clear(&buff, 0));

	/* Buff clear with resizing */
	buffAdd(&buff, data, strlen(data));
	buffClear(&buff, 10);
	TEST("buffClear - with resizing below min", check_clear(&buff, BUFF_DEFAULT_SIZE));

	/* Buff clear with resizing */
	buffNew(&buff, BUFF_DEFAULT_SIZE * 2);
	buffAdd(&buff, data, strlen(data));
	buffClear(&buff, 10);
	TEST("buffClear - with resizing", check_clear(&buff, BUFF_DEFAULT_SIZE));

	/* Resize the buffer with data in it */
	size_t size = BUFF_DEFAULT_SIZE + 10;
	char * lots_of_a = malloc(size);
	memset(lots_of_a, 'A', size);

	buffNew(&buff, BUFF_DEFAULT_SIZE * 2);
	buffAdd(&buff, lots_of_a, size);
	TEST("buffShrink - add data", (buff.size < size || buff.used != size || memcmp(buff.data, lots_of_a, size)));

	buffShrink(&buff, 0);
	TEST("buffShrink - check result", (buff.size < size || buff.used != size || memcmp(buff.data, lots_of_a, size)));
	free(lots_of_a);

	buffFree(&buff);

	/* Concat two buffers together */
	buff_t buff2 = {0};
	data = "Hello ";
	buffAdd(&buff, data, strlen(data));

	data = "World.";
	buffAdd(&buff2, data, strlen(data));

	buffAddBuff(&buff, &buff2);

	data = "Hello World.";
	expected.size = strlen(data);
	expected.used = strlen(data);
	expected.data = data;

	TEST("buffAddBuff", check_buffer(&buff, &expected));

	/* Remove the start of a buffer */
	data = "The quick brown fox jumps over the lazy dog";
	buffAdd(&buff, data, strlen(data));
	buffRemove(&buff, 16, 0);

	data = "fox jumps over the lazy dog";
	expected.data = data;
	expected.used = strlen(data);
	expected.size = strlen(data);

	TEST("buffRemove", check_buffer(&buff, &expected));

	/* Remove test, with shrink */
	size = BUFF_DEFAULT_SIZE * 4;
	char *largeBuffer = malloc(size);
	memset(largeBuffer, 'B', size);
	memset(largeBuffer, 'A', BUFF_DEFAULT_SIZE);

	buffAdd(&buff, largeBuffer, size);

	memset(largeBuffer, 'B', size - BUFF_DEFAULT_SIZE);

	expected.data = largeBuffer;
	expected.size = BUFF_DEFAULT_SIZE * 3;
	expected.used = BUFF_DEFAULT_SIZE * 3;

	buffRemove(&buff, BUFF_DEFAULT_SIZE, BUFF_DEFAULT_SIZE * 3);
	TEST("buffRemove- with shrink", check_buffer(&buff, &expected));
	free(largeBuffer);
}