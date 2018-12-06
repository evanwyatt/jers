/* Copyright (c) 2018 Evan Wyatt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <jers.h>
#include <server.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <sys/uio.h>
#include <time.h>
#include <glob.h>

#define STATE_DIV_FACTOR 10000

/* Escape / unescape newlines and backslash characters
 *  - A static buffer is used to hold the escaped string,
 *    so needs to copied to if required */

char * escapeString(const char * string) {
	static char * escaped = NULL;
	static size_t escaped_size = 0;
	size_t string_length = strlen(string);
	const char * temp = string;
	char * dest;

	/* Assume we have to escape everything */
	if (escaped_size <= string_length * 2 ) {
		escaped_size = string_length *2;
		escaped = realloc(escaped, escaped_size);
	}

	dest = escaped;

	while (*temp != '\0') {
		if (*temp == '\\') {
			*dest++ = '\\';
			*dest++ = '\\';
		} else if (*temp == '\n') {
			*dest++ = '\\';
			*dest++ = 'n';
		} else {
			*dest++ = *temp;
		}

		temp++;
	}

	*dest = '\0';

	return escaped;
}

void unescapeString(char * string) {
	char * temp = string;

	while (*temp != '\0') {
		if (*temp != '\\') {
			temp++;
			continue;
		}

		if (*(temp + 1) == 'n') {
			*temp = '\n';
		} else if (*(temp + 1) == '\\') {
			*temp = '\\';
		}

		memmove(temp + 1, temp + 2, strlen(temp + 2) + 1);
		temp+=2;
	}
}

/* Functions to save/recover the state to/from disk
 *   The state journals (journal.yymmdd) are written to when commands are received
 *   These commands are then applied to the job/queue/res files as needed */

int openStateFile(void) {
	char state_file[PATH_MAX];
	int fd;
	int flags = O_CREAT | O_RDWR;

	if (!server.state_dir)
		error_die("No state directory specified");

	server.state_count++;
	snprintf(state_file, sizeof(state_file), "%s/journal.%d", server.state_dir, server.state_count); //todo: error checking
	fd = open(state_file, flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

	if (fd == -1) {
		error_die("Failed to open state file: %s", strerror(errno));
	}

	return fd;
}

/* Lightweight function to save the current command to disk (& flush it, if in sync mode)*/

int stateSaveCmd(char * cmd, int cmd_len) {
	ssize_t bytes_written = 0;
	off_t start_offset;
	//struct timespec tp = {0};
	//time_t t;
	//static int current_day = -1;

	//clock_gettime(CLOCK_REALTIME_COARSE, &tp);

	//t = tp.tv_sec;
	//struct tm * tm = localtime(&t);

	//if (tm->tm_mday)

	if (server.state_fd < 0) {
		server.state_fd = openStateFile();
	}

	struct iovec iov[3];
	iov[0].iov_base = " ";
	iov[0].iov_len	= 1;
	iov[1].iov_base = cmd;
	iov[1].iov_len	= cmd_len;
	iov[2].iov_base = "\n";
	iov[2].iov_len	= 1;

	start_offset = lseek(server.state_fd, 0, SEEK_CUR);

	do {
		bytes_written = writev(server.state_fd, iov, 3);
	} while (bytes_written < 0 && errno == EINTR);

	if (bytes_written < 0) {
		error_die("Failed to write to state journal: %s", strerror(errno));
	}

	if (server.flush.defer == 0) {
		fdatasync(server.state_fd);
	}

	/* Save the offset of the /start/ of the write we just did.
	 * We will use that later when the changes are persisted to disk */

	server.journal.last_commit = start_offset;
	server.flush.dirty = 1;
	return 0;
}

/* After loading all the queues/jobs/resources from disk,
 * we need go through the journals on disk, checking what we need to replay.
 * When dirty objects are written to disk, the journal is marked with a '*' character
 * in the first position corrisponding to the LAST entry we know made it to disk.
 *
 * We need to scan through the journals, newest first, looking for the last '*'
 * and reapplying any commands after that (potentially across journal files) */

void stateReplayJournal(void) {
	print_msg(JERS_LOG_INFO, "Recovering state from journal files");

	int rc = 0;
	int i;
	glob_t journalGlob;
	char pattern[PATH_MAX];

	sprintf(pattern, "%s/journal.*", server.state_dir);
	print_msg(JERS_LOG_DEBUG, "Searching: %s", pattern);

	rc = glob(pattern, 0, NULL, &journalGlob);

	if (rc != 0) {
		if (rc == GLOB_NOMATCH){
			print_msg(JERS_LOG_WARNING, "No journals to load from disk.");
			globfree(&journalGlob);
			return;
		}

		error_die("Failed to glob() journal files %s : %s\n", pattern, strerror(errno));
	}

	for (i = 0; i < journalGlob.gl_pathc; i++) {
		print_msg(JERS_LOG_DEBUG, "Checking journal: %s", journalGlob.gl_pathv[i]);



	}

	globfree(&journalGlob);
	print_msg(JERS_LOG_INFO, "Finished recovery from journal files");
}

int stateSaveJob(struct job * j) {
	char filename[PATH_MAX];
	char new_filename[PATH_MAX];
	FILE * f;
	int i;
	int directory = j->jobid / STATE_DIV_FACTOR;

	sprintf(filename, "%s/jobs/%d/%d.job", server.state_dir, directory, j->jobid);
	sprintf(new_filename, "%s/jobs/%d/%d.new", server.state_dir, directory, j->jobid);

	f = fopen(new_filename, "w");

	if (f == NULL) {
		fprintf(stderr, "Failed to open job file %s : %s\n", filename, strerror(errno));
		return 1;
	}

	fprintf(f, "JOBNAME %s\n", escapeString(j->jobname));
	fprintf(f, "QUEUENAME %s\n", escapeString(j->queue->name));
	fprintf(f, "SUBMITTIME %ld\n", j->submit_time);

	fprintf(f, "ARGC %d\n", j->argc);

	for (i = 0; i < j->argc; i++) {
		fprintf(f, "ARGV[%d] %s\n", i, escapeString(j->argv[i]));
	}

	if (j->shell)
		fprintf(f, "SHELL %s\n", escapeString(j->shell));

	if (j->pre_cmd)
		fprintf(f, "PRECMD %s\n", escapeString(j->pre_cmd));

	if (j->post_cmd)
		fprintf(f, "POSTCMD %s\n", escapeString(j->post_cmd));

	if (j->stdout)
		fprintf(f, "STDOUT %s\n", escapeString(j->stdout));

	if (j->stderr)
		fprintf(f, "STDERR %s\n", escapeString(j->stderr));

	if (j->env_count) {

		fprintf(f, "ENVC %d\n", j->env_count);

		for (i = 0; i < j->env_count; i++)
			fprintf(f, "ENV[%d] %s\n", i, j->envs[i]);

	}

	if (j->uid)
		fprintf(f, "UID %d\n", j->uid);

	if (j->nice)
		fprintf(f, "NICE %d\n", j->nice);

	if (j->state)
		fprintf(f, "STATE %d\n", j->state);

	if (j->priority)
		fprintf(f, "PRIORITY %d\n", j->priority);

	if (j->defer_time)
		fprintf(f, "DEFERTIME %ld\n", j->defer_time);

	if (j->start_time)
		fprintf(f, "FINISHTIME %ld\n", j->start_time);

	if (j->finish_time)
		fprintf(f, "FINISHTIME %ld\n", j->finish_time);

	fflush(f);
	fsync(fileno(f));
	fclose(f);

	rename(new_filename, filename);
	return 0;
}

int stateSaveQueue(struct queue * q) {
	char filename[PATH_MAX];
	char new_filename[PATH_MAX];
	FILE * f;

	sprintf(filename, "%s/queues/%s.queue", server.state_dir, q->name);
	sprintf(new_filename, "%s/queues/%s.new", server.state_dir, q->name);

	f = fopen(new_filename, "w");

	if (f == NULL) {
		fprintf(stderr, "Failed to open queue file %s : %s\n", filename, strerror(errno));
		return 1;
	}

	fprintf(f, "NAME %s\n", q->name);
	fprintf(f, "DESC %s\n", q->desc);
	fprintf(f, "JOBLIMIT %d\n", q->job_limit);
	fprintf(f, "PRIORITY %d\n", q->priority);
	fprintf(f, "HOST %s\n", q->host);

	if (server.defaultQueue == q)
		fprintf(f, "DEFAULT 1\n");

	fflush(f);
	fsync(fileno(f));
	fclose(f);

	rename(new_filename, filename);
	return 0;
}

int stateSaveResource(struct resource * r) {
	char filename[PATH_MAX];
	char new_filename[PATH_MAX];
	FILE * f;

	sprintf(filename, "%s/resources/%s.resource", server.state_dir, r->name);
	sprintf(new_filename, "%s/resources/%s.new", server.state_dir, r->name);

	f = fopen(new_filename, "w");

	if (f == NULL) {
		fprintf(stderr, "Failed to open resource file %s : %s\n", filename, strerror(errno));
		return 1;
	}

	fprintf(f, "COUNT %d\n", r->count);

	fflush(f);
	fsync(fileno(f));
	fclose(f);

	rename(new_filename, filename);
	return 0;
}

void stateSaveToDiskChild(struct job ** jobs, struct queue ** queues, struct resource ** resources) {
	int64_t i;

	print_msg(JERS_LOG_DEBUG, "Background save jobs:%p queues:%p resources:%p", jobs, queues, resources);

	/* Save the queues and resources first, to avoid having to handle
	 * situations where we might have have to recover jobs that reference
	 * queues and resources that don't exist */

	for (i = 0; i < server.flush_resources; i++)
		stateSaveResource(resources[i]);

	for (i = 0; i < server.flush_queues; i++)
		stateSaveQueue(queues[i]);

print_msg(JERS_LOG_DEBUG, "Background save jobs: %d", server.flush_jobs);

	for (i = 0; i < server.flush_jobs; i++)
		stateSaveJob(jobs[i]);
}

/* This function is responsible for commiting dirty objects to disk.
 * - This is done by creating a list of the dirty objects, then
 *   forking off so the writes are done in the background */

void stateSaveToDisk(void) {
	static uint64_t startTime = 0;
	uint64_t now = getTimeMS();

	static struct job ** dirtyJobs = NULL;
	static struct queue ** dirtyQueues = NULL;
	static struct resource ** dirtyResources = NULL;

	/* If pid is populated we kicked off a save previously */
	if (server.flush.pid) {
		int rc = 0;
		pid_t retPid = 0;

		if ((retPid = waitpid(server.flush.pid, &rc, WNOHANG)) > 0) {
			int status = 0, signo = 0;

			if (WIFEXITED(rc)) {
				status = WEXITSTATUS(rc);
				if (status)
					error_die("Background save failed. ExitCode:%d", status);
			}
			else if (WIFSIGNALED(rc)) {
				signo = WTERMSIG(rc);
				error_die("Background save failed. Signaled:%d", signo);
			} else {
				error_die("Background save failed - Unknown reason.");
			}

			/* Here if the background process finished sucessfully */

			/* Clear the flushing flag on the objects */
			if (server.flush_jobs) {
				int32_t i;
				for (i = 0; i < server.flush_jobs; i++)
					dirtyJobs[i]->internal_state &= ~JERS_JOB_FLAG_FLUSHING;

				free(dirtyJobs);
			}

			if (server.flush_queues) {
				int32_t i;
				for (i = 0; i < server.flush_queues; i++)
					dirtyQueues[i]->internal_state &= ~JERS_JOB_FLAG_FLUSHING;

				free(dirtyQueues);
			}

			if (server.flush_resources) {
				int32_t i;
				for (i = 0; i < server.flush_resources; i++)
					dirtyResources[i]->internal_state &= ~JERS_JOB_FLAG_FLUSHING;

				free(dirtyResources);
			}

			/* Clear our active flush counts  */
			server.flush_jobs = server.flush_queues = server.flush_resources = 0;

			print_msg(JERS_LOG_DEBUG, "Background save complete. Took %dms\n", now - startTime);

			server.flush.pid = 0;
			startTime = 0;
			dirtyJobs = NULL;
			dirtyQueues = NULL;
			dirtyResources = NULL;
			return;
		}

		/* A previous save is still running... */
		print_msg(JERS_LOG_DEBUG, "Background save still running. Has been %ldms", now - startTime);

		//TODO: Sanity checks here?

		return;
	}

	if (server.dirty_jobs == 0 && server.dirty_queues == 0 && server.dirty_resources == 0)
		return;

	print_msg(JERS_LOG_DEBUG, "Starting background save to disk");

	server.flush_jobs = server.dirty_jobs;
	server.flush_queues = server.dirty_queues;
	server.flush_resources = server.dirty_resources;

	server.dirty_jobs = server.dirty_queues = server.dirty_resources = 0;

	/* Check for dirty objects - saving the references to flush to disk.
	 * We clear the dirty flags here, and set a flushing state as we might
	 * make other changes to these objects while they are being saved to disk. */

	if (server.flush_jobs) {
		int i = 0;
		struct job * j = NULL;
		dirtyJobs = calloc(sizeof(struct job *) * (server.flush_jobs + 1), 1);

		for (j = server.jobTable; j != NULL; j = j->hh.next) {
			if (j->dirty) {
				dirtyJobs[i++] = j;
				j->dirty = 0;
				j->internal_state |= JERS_JOB_FLAG_FLUSHING;
			}
		}
	}

	if (server.flush_queues) {
		int i = 0;
		struct queue * q = NULL;
		dirtyQueues = calloc(sizeof(struct queue *) * server.flush_queues + 1, 1);

		for (q = server.queueTable; q != NULL; q = q->hh.next) {
			if (q->dirty) {
				dirtyQueues[i++] = q;
				q->dirty = 0;
				q->internal_state |= JERS_JOB_FLAG_FLUSHING;
			}
		}
	}

	if (server.flush_resources) {
		int i = 0;
		struct resource * r = NULL;
		dirtyResources = calloc(sizeof(struct resource *) * server.flush_resources + 1, 1);

		for (r = server.resTable; r != NULL; r = r->hh.next) {
			if (r->dirty) {
				dirtyResources[i++] = r;
				r->dirty = 0;
				r->internal_state |= JERS_JOB_FLAG_FLUSHING;
			}
		}
	}

	startTime = getTimeMS();

	server.flush.pid = fork();

	if (server.flush.pid == -1) {
		error_die("stateSaveToDisk: failed to fork background task: %s", strerror(errno));
	}

	if (server.flush.pid == 0) {
		stateSaveToDiskChild(dirtyJobs, dirtyQueues, dirtyResources);
		free(dirtyJobs);
		free(dirtyQueues);
		free(dirtyResources);

		/* Now we need to mark the journal that we commited all those transactions to disk */
		if(pwrite(server.state_fd, "*", 1, server.journal.last_commit) != 1) {
			/* This isn't too catastrophic, we might just end up replaying extra transactions if we crash */
			print_msg(JERS_LOG_WARNING, "Background save: Failed to write marker to journal: %s\n", strerror(errno));
		}

		print_msg(JERS_LOG_DEBUG, "Background save complete. Jobs:%d Queues:%d Resources:%d",
			server.flush_jobs, server.flush_queues, server.flush_resources);

		fdatasync(server.state_fd);
		/* We are the forked child, just exit */
		_exit(0);
	}

	/* Parent process - All done for now. We'll check on the child later */
	return;
}

/* Return a key/value from the provided line
 * 'line' is modified and the pointers returned should not be freed */

int loadKeyValue (char * line, char **key, char ** value, int * index) {
	char * comment = NULL;
	char * l_key = NULL;
	char * l_value = NULL;
	char * temp;

	/* Remove a trailing newline if present */
	line[strcspn(line, "\n")] = '\0';

	/* Remove any '#' comments from the line */
	if ((comment = strchr(line, '#'))) {
		*comment = '\0';
	}

	removeWhitespace(line);

	if (!*line) {
		/* Empty line*/
		*key = NULL;
		*value = NULL;
		return 0;
	}

	l_key = line;
	l_value = strpbrk(l_key, " \t");

	if (l_value) {
		*l_value = '\0';
		l_value++;
		removeWhitespace(l_value);
	}

	/* Does the key have and index? ie KEY[1] */

	temp = strchr(l_key, '[');

	if (temp) {
		*temp = '\0';
		if (index)
			*index = atoi(temp + 1);
	}

	/* Need to unescape the value. We would have escaped '\n' & '\' characters */
	unescapeString(l_value);

	*key = l_key;
	*value = l_value;

	return 0;
}

/* Create a directory if it doesn't exist */
void createDir(char * path) {
	struct stat buf;

	if (stat(path, &buf) != 0) {
		if (errno != ENOENT) {
			error_die("Failed to check state directory %s: %s", path, strerror(errno));
		}

		/* Here is it doesn't exist */
		if (mkdir(path, S_IRWXU|S_IRGRP|S_IXGRP) != 0) {
			error_die("Failed to create required directory %s : %s", path, strerror(errno));
		}

	} else if (!(buf.st_mode &S_IFDIR)) {
		error_die("Wanted to create directory: %s but it already exists as a file?", path);
	}

	/* Here if it already existed, or we created it.*/
	return;
}

/* Initialise the state directories, ensuring all needed directories exist, etc. */

void stateInit(void) {
	char tmp[PATH_MAX];

	print_msg(JERS_LOG_DEBUG, "Initialising state directories");

	/* Job dirs first. */
	int highest_dir = server.highest_jobid / STATE_DIV_FACTOR;
	int i;

	int len = sprintf(tmp, "%s/jobs", server.state_dir);
	createDir(tmp);

	for (i = 0; i < highest_dir; i++) {
		sprintf(tmp + len, "/%d", i);
		createDir(tmp);
	}

	/* Queue directory */
	sprintf(tmp, "%s/queues", server.state_dir);
	createDir(tmp);

	/* Resources directory */
	sprintf(tmp, "%s/resources", server.state_dir);
	createDir(tmp);

}

/* Read through the current state files converting the commands
 *  to the appropriate job/queue/res files */

int stateLoadJob(char * fileName) {
	FILE * f = NULL;
	char * line = NULL;
	size_t line_size = 0;
	ssize_t len;
	jobid_t jobid = 0;
	char * temp;
	int state = 0;

	f = fopen(fileName, "r");

	if (!f) {
		error_die("Failed to open job file %s: %s\n", strerror(errno));
	}

	temp = strrchr(fileName, '/');

	if (temp == NULL) {
		error_die("Failed to determine jobid from filename %s", fileName);
	}

	jobid = atoi(temp + 1); // + 1 to move past the '/'

	print_msg(JERS_LOG_DEBUG, "Loading job %d from file", jobid);

	struct job * j = calloc(sizeof(struct job), 1);
	j->jobid = jobid;

	while((len = getline(&line, &line_size, f)) != -1) {

		char *key, *value;
		int index = 0;

		if (loadKeyValue(line, &key, &value, &index)) {
			error_die("Failed to parse job file: %s", fileName);
		}

		if (strcmp(key, "JOBNAME") == 0) {
			j->jobname = strdup(value);
		} else if (strcmp(key, "QUEUENAME") == 0) {
			HASH_FIND_STR(server.queueTable, value, j->queue);

			if (!j->queue) {
				error_die("Error loading jobid %d - Queue '%s' does not exist", jobid, value);
			}
		} else if (strcmp(key, "SHELL") == 0) {
			j->shell = strdup(value);
		} else if (strcmp(key, "PRECMD") == 0) {
			j->pre_cmd = strdup(value);
		} else if (strcmp(key, "POSTCMD") == 0) {
			j->post_cmd = strdup(value);
		} else if (strcmp(key, "STDOUT") == 0) {
			j->stdout = strdup(value);
		} else if (strcmp(key, "STDERR") == 0) {
			j->stderr = strdup(value);
		} else if (strcmp(key, "ARGC") == 0) {
			j->argc = atoi(value);
			j->argv = malloc(sizeof(char *) * j->argc);
		} else if (strcmp(key, "ARGV") == 0) {
			j->argv[index] = strdup(value);
		} else if (strcmp(key, "ENVC") == 0) {
			j->env_count = atoi(value);
			j->envs = malloc (sizeof(char *) * j->env_count);
		} else if (strcmp(key, "ENV") == 0) {
			j->envs[index] = strdup(value);
		} else if (strcmp(key, "UID") == 0) {
			j->uid = atoi(value);
		} else if (strcmp(key, "NICE") == 0) {
			j->nice = atoi(value);
		} else if (strcmp(key, "STATE") == 0) {
			state = atoi(value);
		} else if (strcmp(key, "PRIORITY") == 0) {
			j->priority = atoi(value);
		} else if (strcmp(key, "SUBMITTIME") == 0) {
			j->submit_time = atoi(value);
		} else if (strcmp(key, "DEFERTIME") == 0) {
			j->defer_time = atoi(value);
		} else if (strcmp(key, "FINISHTIME") == 0) {
			j->finish_time = atoi(value);
		} else if (strcmp(key, "STARTTIME") == 0) {
			j->start_time = atoi(value);
		}
	}

	if (len == -1 && feof(f) == 0) {
		error_die("Error reading job file %s:%s\n", fileName, strerror(errno));
	}

	if (j->queue == NULL) {
		error_die("Error loading job %d from file - No queue specified", j->jobid);
	}

	if (state == 0)
		state = JERS_JOB_PENDING;

	addJob(j, state, 0);

	free(line);
	fclose(f);

	return 0;
}

int stateLoadJobs(void) {
	int rc;
	int i;
	jobid_t loaded = 0;
	char pattern[PATH_MAX];
	glob_t jobFiles;

	sprintf(pattern, "%s/jobs/*/*.job", server.state_dir);

	print_msg(JERS_LOG_INFO, "Loading jobs from %s\n", pattern);

	rc = glob(pattern, 0, NULL, &jobFiles);

	if (rc != 0) {
		if (rc == GLOB_NOMATCH){
			print_msg(JERS_LOG_WARNING, "No jobs loaded from disk.");
			globfree(&jobFiles);
			return 0;
		}

		error_die("Failed to glob() job files from %s : %s\n", pattern, strerror(errno));
	}

	for (i = 0; i < jobFiles.gl_pathc; i++) {
		stateLoadJob(jobFiles.gl_pathv[i]);
		loaded++;
	}

	print_msg(JERS_LOG_INFO, "Loaded %d jobs.\n", loaded);

	globfree(&jobFiles);
	return 0;
}

int stateLoadQueue(char * fileName) {
	FILE * f;
	char * name = NULL;
	char * ext;
	struct queue * q;
	char * line = NULL;
	size_t lineSize = 0;
	int def = 0;
	char * fileNameCpy = strdup(fileName);

	f = fopen(fileName, "r");

	if (!f) {
		error_die("stateLoadQueue: Failed to open queue file %s : %s", fileName, strerror(errno));
	}

	/* Derive the name from the filename */
	name = strrchr(fileNameCpy, '/');

	if (!name) {
		error_die("stateLoadQueue: Failed to derive queuename from file: %s : %s", fileName, strerror(errno));
	}

	name++;
	ext = strrchr(name, '.');

	if (!ext) {
		error_die("stateLoadQueue: Failed to derive queuename from file: %s : %s", fileName, strerror(errno));
	}

	*ext = 0;

	q = calloc(sizeof(struct queue), 1);
	q->name = strdup(name);
	q->job_limit = JERS_QUEUE_DEFAULT_LIMIT;
	q->priority = JERS_QUEUE_DEFAULT_PRIORITY;

	/* Read the contents and get the details */
	ssize_t len;
	while((len = getline(&line, &lineSize, f)) != -1) {

		char * key = NULL, *value = NULL;
		int index;

		if (loadKeyValue(line, &key, &value, &index)) {
			error_die("stateLoadQueue: Error parsing queue file: %s\n", fileName);
		}

		if (!key || !value) {
			continue;
		}

		if (strcasecmp(key, "DESC") == 0) {
			q->desc = strdup(value);
		} else if (strcasecmp(key, "JOBLIMIT") == 0) {
			q->job_limit = atoi(value);
		} else if (strcasecmp(key, "PRIORITY") == 0) {
			q->priority = atoi(value);
		} else if (strcasecmp(key, "DEFAULT") == 0) {
			def = atoi(value);
		} else if (strcasecmp(key, "HOST") == 0) {
			q->host = strdup(value);
		} else {
			print_msg(JERS_LOG_WARNING, "stateLoadQueue: skipping unknown config '%s' for queue %s\n", key, name);
		}
	}

	if (len == -1 && feof(f) == 0) {
		error_die("Error reading queue file %s: %s\n", fileName, strerror(errno));
	}

	free(line);

	if (q->host == NULL) {
		error_die("Error loading queue %s from disk. Expected to find HOST key in %s", q->name, fileName);
	}

	/* All loaded, add it to the queueTable */
	if (addQueue(q, def, 0))
		error_die("Failed to add queue '%s'", q->name);

	free(fileNameCpy);

	return 0;
}

/* Load all the queues from the on disk representation */

int stateLoadQueues(void) {
	int rc;
	int i;
	char pattern[PATH_MAX];
	glob_t qFiles;

	sprintf(pattern, "%s/queues/*.queue", server.state_dir);

	print_msg(JERS_LOG_INFO, "Loading queues from %s\n", pattern);

	rc = glob(pattern, 0, NULL, &qFiles);

	if (rc != 0) {
		if (rc == GLOB_NOMATCH){
			print_msg(JERS_LOG_WARNING, "No queues loaded from disk.");
			globfree(&qFiles);
			return 0;
		}

		error_die("Failed to glob() queue files from %s : %s\n", pattern, strerror(errno));
	}

	for (i = 0; i < qFiles.gl_pathc; i++) {
		stateLoadQueue(qFiles.gl_pathv[i]);
	}

	globfree(&qFiles);
	return 0;
}

int stateLoadRes(char * file_name) {
	FILE * f;
	char * name = NULL;
	char * ext;
	struct resource * r;
	char * line = NULL;
	size_t lineSize = 0;
	char * file_name_cpy = strdup(file_name);

	f = fopen(file_name, "r");

	if (!f) {
		error_die("stateLoadRes: Failed to open resource file %s : %s", file_name, strerror(errno));
	}

	/* Derive the name from the filename */
	name = strrchr(file_name_cpy, '/');

	if (!name) {
		error_die("stateLoadRes: Failed to derive resource name from file: %s : %s", file_name, strerror(errno));
	}

	name++;
	ext = strrchr(name, '.');

	if (!ext) {
		error_die("stateLoadRes: Failed to derive resource name from file: %s : %s", file_name, strerror(errno));
	}

	*ext = 0;

	r = calloc(sizeof(struct resource), 1);
	r->name = strdup(name);

	/* Read the contents and get the details */
	ssize_t len;
	while((len = getline(&line, &lineSize, f)) != -1) {

		char * key = NULL, *value = NULL;
		int index;

		if (loadKeyValue(line, &key, &value, &index)) {
			error_die("stateLoadRes: Error parsing resource file: %s\n", file_name);
		}

		if (!key || !value) {
			continue;
		}

		if (strcasecmp(key, "COUNT") == 0) {
			r->count = atoi(value);
		} else {
			print_msg(JERS_LOG_WARNING, "stateLoadRes: skipping unknown config '%s' for resource %s\n", key, name);
		}
	}

	if (len == -1 && feof(f) == 0) {
		error_die("Error reading resource file %s: %s\n", file_name, strerror(errno));
	}

	fclose(f);
	free(line);

	/* All loaded, add it to the queueTable */
	if (addRes(r, 0))
		error_die("Failed to add resource %s", name);

	free(file_name_cpy);

	return 0;
}

int stateLoadResources(void) {
	int rc;
	int i;
	int loaded = 0;
	char pattern[PATH_MAX];
	glob_t resFiles;

	sprintf(pattern, "%s/resources/*.resource", server.state_dir);

	print_msg(JERS_LOG_INFO, "Loading resources from %s\n", pattern);

	rc = glob(pattern, 0, NULL, &resFiles);

	if (rc != 0) {
		if (rc == GLOB_NOMATCH){
			globfree(&resFiles);
			return 0;
		}

		error_die("Failed to glob() resource files from %s : %s\n", pattern, strerror(errno));
	}

	for (i = 0; i < resFiles.gl_pathc; i++) {
		stateLoadRes(resFiles.gl_pathv[i]);
		loaded++;
	}

	print_msg(JERS_LOG_INFO, "Loaded %d jobs.\n", loaded);

	globfree(&resFiles);
	return 0;
}

void changeJobState(struct job * j, int new_state, int dirty) {

	/* Adjust the old state */
	switch (j->state) {
		case JERS_JOB_RUNNING:
			server.stats.running--;
			j->queue->stats.running--;
			break;

		case JERS_JOB_PENDING:
			server.stats.pending--;
			j->queue->stats.pending--;
			break;

		case JERS_JOB_DEFERRED:
			server.stats.deferred--;
			j->queue->stats.deferred--;
			break;

		case JERS_JOB_HOLDING:
			server.stats.holding--;
			j->queue->stats.holding--;
			break;

		case JERS_JOB_COMPLETED:
			server.stats.completed--;
			j->queue->stats.completed--;
			break;

		case JERS_JOB_EXITED:
			server.stats.exited--;
			j->queue->stats.exited--;
			break;
	}

	/* And the new state */

	switch (new_state) {
		case JERS_JOB_RUNNING:
			server.stats.running++;
			j->queue->stats.running++;
			break;

		case JERS_JOB_PENDING:
			server.stats.pending++;
			j->queue->stats.pending++;
			break;

		case JERS_JOB_DEFERRED:
			server.stats.deferred++;
			j->queue->stats.deferred++;
			break;

		case JERS_JOB_HOLDING:
			server.stats.holding++;
			j->queue->stats.holding++;
			break;

		case JERS_JOB_COMPLETED:
			server.stats.completed++;
			j->queue->stats.completed++;
			break;

		case JERS_JOB_EXITED:
			server.stats.exited++;
			j->queue->stats.exited++;
			break;
	}

	j->state = new_state;

	/* Mark it as dirty */
	if (dirty) {
		if (!j->dirty) {
			server.dirty_jobs++;
			server.candidate_recalc = 1;
		}

		j->dirty = 1;
	}
}
