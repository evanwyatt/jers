#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

extern char ** environ;

int main(int argc, char * argv[]) {
	int fd;
	int i = 0;

	if (argc <= 1) {
		return 1;
	}

	fd = atoi(argv[1]);

	while (environ[i]) {
		write(fd, environ[i], strlen(environ[i]) + 1); // +1 to write the NULL
		i++;
	}

	close(fd);
	return 0;
}

