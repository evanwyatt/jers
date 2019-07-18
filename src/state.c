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

#include "server.h"
#include "jers.h"
#include "common.h"
#include "commands.h"

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
#include <libgen.h>

#define STATE_DIV_FACTOR 10000

int resourceStringToResource(const char * string, struct jobResource * res);
int flushDir(char *path);
int flushStateDirs(void);

/* Functions to save/recover the state to/from disk
 *   The state journals (journal.yymmdd) are written to when commands are received
 *   These commands are then applied to the job/queue/res files as needed */

static off_t findJournalEnd(int fd) {
	char data[1024];
	int len;
	off_t offset = 0;

	while ((len = read(fd, data,sizeof(data))) > 0) {
		if (data[len - 1] == 0) {
			/* This chunk should contain the last record */
			int i;
			for (i = 0; i < len && data[i]; i++) {}

			if (i >= len)
				error_die("Failed to locate end of journal");

			offset += i;
			break;
		}

		offset += len;
	}

	print_msg(JERS_LOG_DEBUG, "Found EOJ at offset %ld", offset);
	return offset;
}

/* To ensure we don't run out of space when writing journal entries, space is pre-allocated.
 *  We ensure we have 2 'extend' blocks at the end of journal:
 *  One is used for writing new journal entries, the other is reserved for if we 
 *  go into 'readonly' mode. This second block allows us to write job start/completion messages
 *  which might have a triggered before readonly mode was activated. */

static int extendJournal(void) {
	off_t new_size = server.journal.size + server.journal.extend_block_size;
	ssize_t fill_size = 0;
	ssize_t written = 0;
	off_t offset = server.journal.size;
	static char *data = NULL;
	static ssize_t data_size = 0;

	if (server.journal.limit == 0)
		new_size += server.journal.extend_block_size;

	/* Need to actually write something to the journal to get the new space allocated */
	fill_size = new_size - server.journal.size;

	print_msg(JERS_LOG_DEBUG, "Attempting to add %ld bytes to the journal. New size: %ld", fill_size, new_size);

	if (data_size < fill_size) {
		data = realloc(data, fill_size);
		memset(data, 0, fill_size);
	}

	while (fill_size) {
		if ((written = pwrite(server.journal.fd, data, fill_size, offset)) == -1) {
			if (errno == ENOSPC) {
				/* No space is left on the device, we need to switch into a read only mode */
				if (server.readonly == 0) {
					print_msg(JERS_LOG_CRITICAL, "*********************************************");
					print_msg(JERS_LOG_CRITICAL, "* Failed to extend journal - Device is full *");
					print_msg(JERS_LOG_CRITICAL, "*        Switching to READONLY mode!        *");
					print_msg(JERS_LOG_CRITICAL, "*********************************************");
					server.readonly = 1;
				}

				return 1;
			} else if (errno != EINTR) {
				/* Another error occurred trying to fill in the file */
				error_die("Failed to extend journal file: %s", strerror(errno));
			}
		}

		fill_size -= written;
		offset += written;
	}

	if (server.readonly) {
		print_msg(JERS_LOG_INFO, "Turning off readonly mode - journal extended.");
		server.readonly = 0;
	}

	server.journal.size = new_size;
	server.journal.limit = server.journal.size - server.journal.extend_block_size;

	return 0;
}

int openStateFile(time_t now) {
	char * state_file = NULL;
	int fd;
	int flags = O_CREAT | O_RDWR;
	int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
	struct tm * tm = NULL;
	struct stat buf;

	tm = localtime(&now);

	if (tm == NULL)
		error_die("Failed to get time for state file name: %s", strerror(errno));

	if (!server.state_dir)
		error_die("No state directory specified");

	asprintf(&state_file, "%s/journal.%d%02d%02d", server.state_dir, 1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday);

	if (state_file == NULL)
		error_die("Failed to open new state file: %s", strerror(errno));

	if ((fd = open(state_file, flags | O_EXCL, mode)) < 0) {
		if (errno != EEXIST)
			error_die("Failed to open state file %s: %s", state_file, strerror(errno));
		
		/* The journal already existed.
		 * We need to find the end of the last written record. */
		if ((fd = open(state_file, flags, mode)) < 0)
			error_die("Failed to open state file %s: %s", state_file, strerror(errno));

		server.journal.len = findJournalEnd(fd);

		if (lseek(fd, server.journal.len, SEEK_SET) != server.journal.len)
			error_die("Failed to seek to end of journal %s: %s", state_file, strerror(errno));

		/* Get the size */
		if (fstat(fd, &buf) != 0)
			error_die("Failed to stat journal file %s: %s", state_file, strerror(errno));

		server.journal.size = buf.st_size;

		/* Work out the limit of this journal */
		server.journal.limit = server.journal.size - server.journal.extend_block_size;

		if (server.journal.limit <= 0)
			server.journal.limit = server.journal.len; // Ensure the next allocation will increase the size

		if (flushDir(server.state_dir) != 0)
			error_die("Failed to flush state directory %s: %s", server.state_dir, strerror(errno));
	} else {
		/* Created a new journal file */
		server.journal.len = 0;
		server.journal.size = 0;
		server.journal.limit = 0;
	}

	free(state_file);
	return fd;
}

time_t getRollOver(time_t now) {
	struct tm * _tm = localtime(&now);

	/* Clear out everything but the year/month/day */
	_tm->tm_sec = _tm->tm_min = _tm->tm_hour = 0;
	_tm->tm_mday++; //mktime will normalize the result. ie. 40 October is changed into 9 November

	return mktime(_tm);
}

/* Function to save the current command to disk (& flush it, if in sync mode)
 * Only 'update' command are written, ie command that modify jobs/queues or resources.
 * 
 * Records are formatted as tab seperated fields. newline, tabs and
 * backslashes are escaped before being written to disk.
 * 
 *  TIME\tUID\tCMDt\tFIELDS...
 *
 * For example:
 *  1544430918.333\t1001\tADD_JOB\tJOBID=12345\tNAME=JOB_1\tARGS[0]=Argument\\twith\\ttabs\n
 *
 * Note: There is a space at the start of line, this space will have a '*' character written
 *       to this position when these transaction have been commited to disk as the job/queue/resource state files
 *       A '$' will be written to this position as a 'End of journal' marker when rotating the journal files */

int stateSaveCmd(uid_t uid, char * cmd, char * msg, jobid_t jobid, int64_t revision) {
	off_t start_offset;
	struct timespec now;
	int len = 0;
	static time_t next_rollover = 0;
	static char *msg_string = NULL;
	static int msg_size = 0;

	clock_gettime(CLOCK_REALTIME_COARSE, &now);

	if (now.tv_sec >= next_rollover) {
		/* Write the End of journal marker and close the current file */
		if (server.journal.fd > 0) {
			write(server.journal.fd, "$\n", 2);
			fdatasync(server.journal.fd);
			close(server.journal.fd);
		}

		/* Work out the next rollover */
		if ((next_rollover = getRollOver(now.tv_sec)) < 0)
			error_die("Failed to determine next journal rollover time");

		server.journal.fd = openStateFile(now.tv_sec);

		if (server.journal.size == 0 || server.journal.len >= server.journal.limit) {
			if (extendJournal())
				return 1;
		}
	}

	/* Save the offset of this new record, so we can write the '*' later if needed */
	start_offset = lseek(server.journal.fd, 0, SEEK_CUR);

	/* Expand the msg */
	while (1) {
		len = snprintf(msg_string, msg_size, " %ld.%03d\t%d\t%s\t%d\t%ld\t%s\n", now.tv_sec, (int)(now.tv_nsec / 1000000), uid, cmd, jobid, revision, msg ? escapeString(msg, NULL):"");

		if (len < msg_size)
			break;

		msg_size = len + 64;
		msg_string = realloc(msg_string, msg_size);

		if (msg_string == NULL)
			error_die("Failed to allocate memory for journal message: %s", strerror(errno));
	}

	/* Do we need to extend the journal? */
	if (server.journal.len + len >= server.journal.limit) {
		extendJournal();
		// Even if extendJournal fails, we want to write this message out. 
	}

	len = write(server.journal.fd, msg_string, len);

	if (len == -1) {
		print_msg(JERS_LOG_CRITICAL, "Failed to write to journal file: %s", strerror(errno));
		return 1;
	}

	server.journal.len += len;

	if (server.flush.defer == 0)
		fdatasync(server.journal.fd);
	else
		server.flush.dirty++;

	server.journal.last_commit = start_offset;
	return 0;
}

off_t checkForLastCommit(char * journal) {
	FILE * f = NULL;
	char * line = NULL;
	size_t line_size = 0;
	ssize_t len = 0;
	off_t last_commit = -1;

	print_msg(JERS_LOG_INFO, "Checking journal: %s", journal);

	if ((f = fopen(journal, "r")) == NULL)
		error_die("Failed to open journal %s: %s", journal, strerror(errno));
	
	while ((len = getline(&line, &line_size, f)) != -1) {
		if (line[0] == '*') {
			last_commit = ftell(f);

			if (last_commit == -1)
				error_die("Failed to get offset of last commit: %s", strerror(errno));
		}
	}

	if (len == -1 && !feof(f))
		error_die("Failed to read line from journal: %s", strerror(errno));

	fclose(f);
	free(line);

	return last_commit;
}

/* Convert a journal entry into a message that can be used for recovering state */

void convertJournalEntry(msg_t *msg, buff_t *message_buffer, char *entry) {
	time_t timestamp_s;
	int timestamp_ms;
	uid_t uid;
	char command[65];
	jobid_t jobid;
	int64_t revision;
	int field_count = 0;
	int msg_offset = 0;

	memset(msg, 0, sizeof(msg_t));

	message_buffer->used = 0;

	if (buffResize(message_buffer, strlen(entry)) != 0)
		error_die("Failed to allocate memory for journal buffer");

	field_count = sscanf(entry, " %ld.%d\t%d\t%64s\t%u\t%ld\t%n", &timestamp_s, &timestamp_ms, (int *)&uid, command, &jobid, &revision, &msg_offset);

	if (field_count != 6) {
		print_msg(JERS_LOG_CRITICAL, "Failed entry (len:%ld): %s", strlen(entry), entry);
		error_die("Failed to load journal entry. Got %d fields, wanted 6\n", field_count);
	}

	strcpy(message_buffer->data, entry + msg_offset);

	if (*message_buffer->data) {
		unescapeString(message_buffer->data);
		message_buffer->used = strlen(message_buffer->data);

		if (load_message(msg, message_buffer) != 0)
			error_die("Failed to load message from journal entry");
	}

	server.recovery.time = timestamp_s;
	server.recovery.uid = uid;
	server.recovery.jobid = jobid;
	server.recovery.revision = revision;

	print_msg(JERS_LOG_DEBUG, "Replaying cmd:%s uid:%d time:%ld jobid:%d revision:%ld", command, uid, timestamp_s, jobid, revision);
}

void replayTransaction(char * line) {
	msg_t msg;
	buff_t buff;

	/* Remove the newline */
	line[strcspn(line, "\n")] = 0;

	/* End of journal marker, nothing to do here */
	if (line[0] == '$')
		return;

	if (line[0] == 0)
		return;

	if (buffNew(&buff, 0) != 0)
		error_die("Failed to resize buffer to replayTransaction");

	/* Load the fields into a msg_t and call the appropriate command handler */
	convertJournalEntry(&msg, &buff, line);

	if (buff.used != 0)
		replayCommand(&msg);

	buffFree(&buff);
	return;
}

/* Read and replay transactions from the journal.
 * 'Offset' is provided for the first file, subsequent files have the offset passed in as
 * a negative number. - All entries should be replayed for these files */ 

void replayJournal(char * journal, off_t offset) {
	FILE * f = NULL;
	char * line = NULL;
	size_t line_size = 0;
	ssize_t len = 0;

	print_msg(JERS_LOG_INFO, "Replaying journal %s", journal);

	if (offset >= 0)
		print_msg(JERS_LOG_DEBUG, "Using journal offset: %ld", offset);

	if ((f = fopen(journal, "r")) == NULL)
		error_die("Failed to open journal %s: %s", journal, strerror(errno));

	if (offset >= 0 && fseek(f, offset, SEEK_SET) != 0)
		error_die("Failed to offset into journal at offset %ld: %s", offset, strerror(errno));
	
	while ((len = getline(&line, &line_size, f)) != -1) {
		replayTransaction(line);
	}

	if (len == -1 && !feof(f))
		error_die("Failed to read line from journal: %s", strerror(errno));

	fclose(f);
	free(line);

	print_msg(JERS_LOG_DEBUG, "Finished replaying journal %s", journal);

	return;
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
	size_t i;
	glob_t journalGlob;
	char pattern[PATH_MAX];
	off_t offset = -1;

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

	if (journalGlob.gl_pathc) {
		for (i = journalGlob.gl_pathc; i > 0 ; i--) {
			if ((offset = checkForLastCommit(journalGlob.gl_pathv[i - 1])) >= 0)
				break;
		}
	}

	/* If we didn't find any offset, we need to replay everything we have */
	if (offset == -1) {
		offset = 0;
		i = 1;
	}

	/* We know which journal to start from, start replaying */
	if (i > 0) {
		server.recovery.in_progress = 1;

		for (; i <= journalGlob.gl_pathc; i++) {
			replayJournal(journalGlob.gl_pathv[i - 1], offset);
			offset = -1;
		}
	}

	globfree(&journalGlob);

	server.recovery.in_progress = 0;
	server.recovery.time = 0;
	server.recovery.uid = 0;
	server.recovery.jobid = 0;

	print_msg(JERS_LOG_INFO, "Finished recovery from journal files");

	/* Now that we have recovered our state, we need to look for any jobs that were in a 'RUN' state when we shutdown (crashed)
	 * We will mark these jobs as 'UNKNOWN', which will require manual intervention to start again. There is a chance
	 * that an agent will log back in and update this state. A job will only have its state updated from an agent if it hasn't
	 * already been modified. ie Restarted or killed */

	struct job * j;

	for (j = server.jobTable; j; j = j->hh.next) {
		if (j->state == JERS_JOB_RUNNING || j->internal_state & JERS_FLAG_JOB_STARTED) {
			j->internal_state &= ~JERS_FLAG_JOB_STARTED;
			changeJobState(j, JERS_JOB_UNKNOWN, NULL, 1);
			print_msg(JERS_LOG_WARNING, "Job %d is now unknown", j->jobid);
		}
	}

	/* Make sure have the latest journal open by writing a dummy command */
	stateSaveCmd(getuid(), "REPLAY_COMPLETE", NULL, 0, 0);
}

int stateDelJob(struct job * j) {
	char filename[PATH_MAX];
	int directory = j->jobid / STATE_DIV_FACTOR;
	sprintf(filename, "%s/jobs/%d/%d.job", server.state_dir, directory, j->jobid);

	if (unlink(filename) != 0)
		print_msg(JERS_LOG_WARNING, "Failed to remove statefile for deleted job %d: %s", j->jobid, strerror(errno));

	return 0;
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

	fprintf(f, "# SAVETIME %ld\n", time(NULL));
	fprintf(f, "REVISION %ld\n", j->obj.revision);

	fprintf(f, "JOBNAME %s\n", escapeString(j->jobname, NULL));
	fprintf(f, "QUEUENAME %s\n", escapeString(j->queue->name, NULL));
	fprintf(f, "SUBMITTIME %ld\n", j->submit_time);

	fprintf(f, "SUBMITTER %d\n", j->submitter);

	fprintf(f, "ARGC %d\n", j->argc);

	for (i = 0; i < j->argc; i++) {
		fprintf(f, "ARGV[%d] %s\n", i, escapeString(j->argv[i], NULL));
	}

	if (j->shell)
		fprintf(f, "SHELL %s\n", escapeString(j->shell, NULL));

	if (j->pre_cmd)
		fprintf(f, "PRECMD %s\n", escapeString(j->pre_cmd, NULL));

	if (j->post_cmd)
		fprintf(f, "POSTCMD %s\n", escapeString(j->post_cmd, NULL));

	if (j->stdout)
		fprintf(f, "STDOUT %s\n", escapeString(j->stdout, NULL));

	if (j->stderr)
		fprintf(f, "STDERR %s\n", escapeString(j->stderr, NULL));

	if (j->env_count) {
		fprintf(f, "ENV_COUNT %d\n", j->env_count);

		for (i = 0; i < j->env_count; i++)
			fprintf(f, "ENV[%d] %s\n", i, j->envs[i]);
	}

	if (j->tag_count) {
		fprintf(f, "TAG_COUNT %d\n", j->tag_count);
		for (i = 0; i < j->tag_count; i++)
			fprintf(f, "TAG[%d] %s\t%s\n", i, j->tags[i].key, j->tags[i].value ? escapeString(j->tags[i].value, NULL) : "");
	}

	if (j->res_count) {
		fprintf(f, "RES_COUNT %d\n", j->res_count);
		for (i = 0; i < j->res_count; i++)
			fprintf(f, "RES[%d] %s:%d\n", i, j->req_resources[i].res->name, j->req_resources[i].needed);
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
		fprintf(f, "STARTTIME %ld\n", j->start_time);

	if (j->finish_time)
		fprintf(f, "FINISHTIME %ld\n", j->finish_time);

	if (j->exitcode)
		fprintf(f, "EXITCODE %d\n", j->exitcode);

	if (j->signal)
		fprintf(f,"SIGNAL %d\n", j->signal);

	/* Usage */
	if (j->finish_time) {
		fprintf(f, "USAGE_UTIME_SEC %ld\n", j->usage.ru_utime.tv_sec);
		fprintf(f, "USAGE_UTIME_USEC %ld\n", j->usage.ru_utime.tv_usec);
		fprintf(f, "USAGE_STIME_SEC %ld\n", j->usage.ru_stime.tv_sec);
		fprintf(f, "USAGE_STIME_USEC %ld\n", j->usage.ru_stime.tv_usec);
		fprintf(f, "USAGE_MAXRSS %ld\n", j->usage.ru_maxrss);
		fprintf(f, "USAGE_MINFLT %ld\n", j->usage.ru_minflt);
		fprintf(f, "USAGE_MAJFLT %ld\n", j->usage.ru_majflt);
		fprintf(f, "USAGE_INBLOCK %ld\n", j->usage.ru_inblock);
		fprintf(f, "USAGE_OUBLOCK %ld\n", j->usage.ru_oublock);
		fprintf(f, "USAGE_NVCSW %ld\n", j->usage.ru_nvcsw);
		fprintf(f, "USAGE_NIVCSW %ld\n", j->usage.ru_nivcsw);
	}

	if (fflush(f)) {
		fclose(f);
		return 1;
	}

	if (fsync(fileno(f))) {
		fclose(f);
		return 1;
	}

	fclose(f);

	if (rename(new_filename, filename) != 0) {
		fprintf(stderr, "Failed to rename '%s' to '%s': %s\n", new_filename, filename, strerror(errno));
		return 1;
	}

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

	fprintf(f, "# SAVETIME %ld\n", time(NULL));

	fprintf(f, "DESC %s\n", q->desc);
	fprintf(f, "JOBLIMIT %d\n", q->job_limit);
	fprintf(f, "PRIORITY %d\n", q->priority);
	fprintf(f, "HOST %s\n", q->host);
	fprintf(f, "REVISION %ld\n", q->obj.revision);

	if (server.defaultQueue == q)
		fprintf(f, "DEFAULT 1\n");

	if (fflush(f)) {
		fclose(f);
		return 1;
	}

	if (fsync(fileno(f))) {
		fclose(f);
		return 1;
	}

	fclose(f);

	if (rename(new_filename, filename) != 0) {
		fprintf(stderr, "Failed to rename '%s' to '%s': %s\n", new_filename, filename, strerror(errno));
		return 1;
	}

	return 0;
}

int stateDelQueue(struct queue * q) {
	char filename[PATH_MAX];
	sprintf(filename, "%s/queues/%s.queue", server.state_dir, q->name);

	if (unlink(filename) != 0)
		print_msg(JERS_LOG_WARNING, "Failed to remove statefile for deleted queue %s: %s", q->name, strerror(errno));

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

	fprintf(f, "# SAVETIME %ld\n", time(NULL));

	fprintf(f, "COUNT %d\n", r->count);
	fprintf(f, "REVISION %ld\n", r->obj.revision);

	if (fflush(f)) {
		fclose(f);
		return 1;
	}

	if (fsync(fileno(f))) {
		fclose(f);
		return 1;
	}

	fclose(f);

	if (rename(new_filename, filename) != 0) {
		fprintf(stderr, "Failed to rename '%s' to '%s': %s\n", new_filename, filename, strerror(errno));
		return 1;
	}

	return 0;
}

int stateDelResource(struct resource * r) {
	char filename[PATH_MAX];
	sprintf(filename, "%s/resources/%s.resource", server.state_dir, r->name);

	if (unlink(filename) != 0)
		print_msg(JERS_LOG_WARNING, "Failed to remove statefile for deleted resource %s: %s", r->name, strerror(errno));

	return 0;
}

int stateSaveToDiskChild(struct job ** jobs, struct queue ** queues, struct resource ** resources) {
	int64_t i;

	setproctitle("jersd_state_save");

	print_msg(JERS_LOG_DEBUG, "Background save jobs:%p queues:%p resources:%p", jobs, queues, resources);

	/* Save the queues and resources first, to avoid having to handle
	 * situations where we might have to recover jobs that reference
	 * queues and resources that don't exist */

	for (i = 0; i < server.flush_resources; i++) {
		if (stateSaveResource(resources[i]))
			return 1;
	}

	for (i = 0; i < server.flush_queues; i++) {
		if (stateSaveQueue(queues[i]))
			return 1;
	}

	for (i = 0; i < server.flush_jobs; i++) {
		if (stateSaveJob(jobs[i]))
			return 1;
	}

	/* Flush any directory we might have touched */
	if (flushStateDirs())
		return 1;

	return 0;
}

/* This function is responsible for commiting dirty objects to disk.
 * - This is done by creating a list of the dirty objects, then
 *   forking off so the writes are done in the background */

void stateSaveToDisk(int block) {
	static uint64_t startTime = 0;
	uint64_t now = getTimeMS();

	static struct job ** dirtyJobs = NULL;
	static struct queue ** dirtyQueues = NULL;
	static struct resource ** dirtyResources = NULL;

	/* If pid is populated we kicked off a save previously */
	if (server.flush.pid) {
		int rc = 0;
		pid_t retPid = 0;

		if ((retPid = waitpid(server.flush.pid, &rc, block? 0 : WNOHANG)) > 0) {
			int status = 0, signo = 0;

			if (WIFEXITED(rc)) {
				status = WEXITSTATUS(rc);
				if (status)
					print_msg(JERS_LOG_CRITICAL, "Background save failed. ExitCode:%d", status);
			}
			else if (WIFSIGNALED(rc)) {
				signo = WTERMSIG(rc);
				error_die("Background save failed. Signaled:%d", signo);
			} else {
				error_die("Background save failed - Unknown reason.");
			}

			/* Clear the flushing flag on the objects */
			if (server.flush_jobs) {
				int64_t i;
				for (i = 0; i < server.flush_jobs; i++)
					dirtyJobs[i]->internal_state &= ~JERS_FLAG_FLUSHING;
			}

			if (server.flush_queues) {
				int64_t i;
				for (i = 0; i < server.flush_queues; i++)
					dirtyQueues[i]->internal_state &= ~JERS_FLAG_FLUSHING;
			}

			if (server.flush_resources) {
				int64_t i;
				for (i = 0; i < server.flush_resources; i++)
					dirtyResources[i]->internal_state &= ~JERS_FLAG_FLUSHING;
			}

			/* If the background save failed, set them all back as dirty */
			if (unlikely(status)) {
				if (server.flush_jobs) {
					int64_t i;
					for (i = 0; i < server.flush_jobs; i++)
						dirtyJobs[i]->obj.dirty = 1;

					server.dirty_jobs = 1;
				}

				if (server.flush_queues) {
					int64_t i;
					for (i = 0; i < server.flush_queues; i++)
						dirtyQueues[i]->obj.dirty = 1;

					server.dirty_queues = 1;
				}

				if (server.flush_resources) {
					int64_t i;
					for (i = 0; i < server.flush_resources; i++)
						dirtyResources[i]->obj.dirty = 1;

					server.dirty_resources = 1;
				}
			}

			/* Clear our active flush counts  */
			server.flush_jobs = server.flush_queues = server.flush_resources = 0;

			free(dirtyJobs);
			free(dirtyQueues);
			free(dirtyResources);

			print_msg(JERS_LOG_DEBUG, "Background save %s. Took %ldms\n", status ? "FAILED":"complete", now - startTime);

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

	if (server.readonly) {
		/* Try extending the journal to see if we can get out of readonly mode */
		if (extendJournal()) {
			print_msg(JERS_LOG_WARNING, "Skipping background save - READONLY mode");
			return;
		}
	}

	print_msg(JERS_LOG_DEBUG, "Starting background save to disk");

	/* Check for dirty objects - saving the references to flush to disk.
	 * We clear the dirty flags here, and set a flushing state as we might
	 * make other changes to these objects while they are being saved to disk. */

	if (server.dirty_jobs) {
		int i = 0;
		struct job * j = NULL;

		dirtyJobs = malloc(sizeof(struct job *) * (HASH_COUNT(server.jobTable) + 1));

		for (j = server.jobTable; j != NULL; j = j->hh.next) {
			if (j->obj.dirty) {
				dirtyJobs[i++] = j;
				j->obj.dirty = 0;
				j->internal_state |= JERS_FLAG_FLUSHING;
			}
		}

		dirtyJobs[i] = NULL;
		server.flush_jobs = i;
	}

	if (server.dirty_queues) {
		int i = 0;
		struct queue * q = NULL;

		dirtyQueues = malloc(sizeof(struct queue *) * (HASH_COUNT(server.queueTable) + 1));

		for (q = server.queueTable; q != NULL; q = q->hh.next) {
			if (q->obj.dirty) {
				dirtyQueues[i++] = q;
				q->obj.dirty = 0;
				q->internal_state |= JERS_FLAG_FLUSHING;
			}
		}

		dirtyQueues[i] = NULL;
		server.flush_queues = i;
	}

	if (server.dirty_resources) {
		int i = 0;
		struct resource * r = NULL;

		dirtyResources = malloc(sizeof(struct resource *) * (HASH_COUNT(server.resTable) + 1));

		for (r = server.resTable; r != NULL; r = r->hh.next) {
			if (r->obj.dirty) {
				dirtyResources[i++] = r;
				r->obj.dirty = 0;
				r->internal_state |= JERS_FLAG_FLUSHING;
			}
		}

		dirtyResources[i] = NULL;
		server.flush_resources = i;
	}

	server.dirty_jobs = server.dirty_queues = server.dirty_resources = 0;

	startTime = getTimeMS();

	server.flush.pid = fork();

	if (server.flush.pid == -1) {
		error_die("stateSaveToDisk: failed to fork background task: %s", strerror(errno));
	}

	if (server.flush.pid == 0) {
		int status = stateSaveToDiskChild(dirtyJobs, dirtyQueues, dirtyResources);
		free(dirtyJobs);
		free(dirtyQueues);
		free(dirtyResources);

		if (status == 0) {
			/* Now we need to mark the journal that we commited all those transactions to disk */
			if (pwrite(server.journal.fd, "*", 1, server.journal.last_commit) != 1) {
				/* This isn't too catastrophic, we might just end up replaying extra transactions if we crash */
				print_msg(JERS_LOG_WARNING, "Background save: Failed to write marker to journal: %s\n", strerror(errno));
			}

			print_msg(JERS_LOG_DEBUG, "Background save complete. Jobs:%ld Queues:%ld Resources:%ld",
				server.flush_jobs, server.flush_queues, server.flush_resources);

			fdatasync(server.journal.fd);
		} else {
			print_msg(JERS_LOG_WARNING, "Background save: Failed.\n");
		}
		/* We are the forked child, just exit */
		_exit(status);
	}

	/* Parent process - All done for now. We'll check on the child later if we aren't told to block */

	if (block) {
		print_msg(JERS_LOG_INFO, "Waiting for background save to complete (blocking)");
		stateSaveToDisk(1);
	}

	return;
}

/* Return a key/value from the provided line
 * 'line' is modified and the pointers returned should not be freed */

int loadKeyValue (char * line, char **key, char ** value, int * index) {
	char * l_key = NULL;
	char * l_value = NULL;
	char * temp;

	/* Remove a trailing newline if present */
	line[strcspn(line, "\n")] = '\0';

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

	/* Does the key have an index? ie KEY[1] */

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

	if (mkdir(path, S_IRWXU|S_IRGRP|S_IXGRP) != 0) {
		if (errno != EEXIST)
			error_die("Failed to create required directory %s : %s", path, strerror(errno));

		if (stat(path, &buf) != 0)
			error_die("Failed to stat directory %s: %s", path, strerror(errno));

		if (!(buf.st_mode &S_IFDIR))
			error_die("Attemped to create directory %s, but it already exists as a file?", path);
	}

	return;
}

/* Flush the specified directory */
int flushDir(char *path) {
	struct stat buf;
	char *temp = NULL;

	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		print_msg(JERS_LOG_WARNING, "flushDir: Failed to open %s: %s", path, strerror(errno));
		return 1;
	}

	if (fstat(fd, &buf) == -1) {
		print_msg(JERS_LOG_WARNING, "flushDir: Failed to stat() %s: %s", path, strerror(errno));
		close(fd);
		return 1;
	}

	/* If this is not a directory, strip off the filename */
	if ((buf.st_mode &S_IFMT) != S_IFDIR) {
		temp = strdup(path);
		path = dirname(temp);

		close(fd);

		fd = open(path, O_RDONLY);

		if (fd < 0) {
			print_msg(JERS_LOG_WARNING, "flushDir: Failed to open directory %s: %s", path, strerror(errno));
			free(temp);
			return 1;
		}
	}

	if (fdatasync(fd)) {
		close(fd);
		free(temp);
		return 1;
	}

	close(fd);

	free(temp);
	return 0;
}

/* Flush all the state directories */
int flushStateDirs(void) {
	char tmp[PATH_MAX];
	int highest_dir = server.max_jobid / STATE_DIV_FACTOR;
	int i;

	if (flushDir(server.state_dir))
		return 1;

	sprintf(tmp, "%s/resources", server.state_dir);
	if (flushDir(tmp))
		return 1;

	sprintf(tmp, "%s/queues", server.state_dir);
	if (flushDir(tmp))
		return 1;

	int len = sprintf(tmp, "%s/jobs", server.state_dir);
	if (flushDir(tmp))
		return 1;

	for (i = 0; i <= highest_dir; i++) {
		sprintf(tmp + len, "/%d", i);
	if (flushDir(tmp))
		return 1;
	}

	return 0;
}

/* Initialise the state directories, ensuring all needed directories exist, etc. */

void stateInit(void) {
	char tmp[PATH_MAX];

	print_msg(JERS_LOG_DEBUG, "Initialising state directories");

	/* Job dirs first. */
	int highest_dir = server.max_jobid / STATE_DIV_FACTOR;
	int i;

	int len = sprintf(tmp, "%s/jobs", server.state_dir);
	createDir(tmp);

	for (i = 0; i <= highest_dir; i++) {
		sprintf(tmp + len, "/%d", i);
		createDir(tmp);
	}

	/* Queue directory */
	sprintf(tmp, "%s/queues", server.state_dir);
	createDir(tmp);

	/* Resources directory */
	sprintf(tmp, "%s/resources", server.state_dir);
	createDir(tmp);

	flushStateDirs();
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

	struct job * j = calloc(sizeof(struct job), 1);
	j->jobid = jobid;
	j->obj.type = JERS_OBJECT_JOB;

	while((len = getline(&line, &line_size, f)) != -1) {

		char *key, *value;
		int index = 0;

		if (loadKeyValue(line, &key, &value, &index)) {
			error_die("Failed to parse job file: %s", fileName);
		}

		if (strcmp(key, "JOBNAME") == 0) {
			j->jobname = strdup(value);
		} else if (strcmp(key, "QUEUENAME") == 0) {
			j->queue = findQueue(value);

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
		} else if (strcmp(key, "ENV_COUNT") == 0) {
			j->env_count = atoi(value);
			j->envs = malloc (sizeof(char *) * j->env_count);
		} else if (strcmp(key, "ENV") == 0) {
			j->envs[index] = strdup(value);
		}else if (strcmp(key, "TAG_COUNT") == 0) {
			j->tag_count = atoi(value);
			j->tags = malloc (sizeof(key_val_t) * j->tag_count);
		} else if (strcmp(key, "TAG") == 0) {
			/* A tag itself is a key value pair seperated by a tab*/
			char * tag_key = value;
			char * tag_value = strchr(value, '\t');
			if (tag_value != NULL) {
				*tag_value = '\0';
				tag_value++;
				j->tags[index].value = strdup(tag_value);
			} else {
				j->tags[index].value = NULL;
			}

			j->tags[index].key = strdup(tag_key);

		} else if (strcmp(key, "RES_COUNT") == 0) {
			j->res_count = atoi(value);
			j->req_resources = malloc(sizeof(struct jobResource) * j->res_count);
		} else if (strcmp(key, "RES") == 0) {
			if (resourceStringToResource(value, &j->req_resources[index]) != 0) {
				error_die("Invalid resource encountered for job %d\n", j->jobid);
			}
		} else if (strcmp(key, "UID") == 0) {
			j->uid = atoi(value);
		} else if (strcmp(key, "SUBMITTER") == 0) {
			j->submitter = atoi(value);
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
		} else if (strcmp(key, "EXITCODE") == 0) {
			j->exitcode = atoi(value);
		} else if (strcmp(key, "SIGNAL") == 0) {
			j->signal = atoi(value);
		} else if (strcmp(key, "REVISION") == 0) {
			j->obj.revision = atol(value);
		} else if (strcmp(key, "USAGE_UTIME_SEC") == 0) {
			j->usage.ru_utime.tv_sec = atol(value);
		} else if (strcmp(key, "USAGE_UTIME_USEC") == 0) {
			j->usage.ru_utime.tv_usec = atol(value);
		} else if (strcmp(key, "USAGE_STIME_SEC") == 0) {
			j->usage.ru_stime.tv_sec = atol(value);
		} else if (strcmp(key, "USAGE_STIME_USEC") == 0) {
			j->usage.ru_stime.tv_usec = atol(value);
		} else if (strcmp(key, "USAGE_MAXRSS") == 0) {
			j->usage.ru_maxrss = atol(value);
		} else if (strcmp(key, "USAGE_MINFLT") == 0) {
			j->usage.ru_minflt = atol(value);
		} else if (strcmp(key, "USAGE_MAJFLT") == 0) {
			j->usage.ru_majflt = atol(value);
		} else if (strcmp(key, "USAGE_INBLOCK") == 0) {
			j->usage.ru_inblock = atol(value);
		} else if (strcmp(key, "USAGE_OUBLOCK") == 0) {
			j->usage.ru_oublock = atol(value);
		} else if (strcmp(key, "USAGE_NVCSW") == 0) {
			j->usage.ru_nvcsw = atol(value);
		} else if (strcmp(key, "USAGE_NIVCSW") == 0) {
			j->usage.ru_nivcsw = atol(value);
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

	if (j->jobid > server.start_jobid)
		server.start_jobid = j->jobid;

	return 0;
}

int stateLoadJobs(void) {
	int rc;
	size_t i;
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

	print_msg(JERS_LOG_INFO, "Loading %ld jobs from disk", jobFiles.gl_pathc);

	for (i = 0; i < jobFiles.gl_pathc; i++)
		stateLoadJob(jobFiles.gl_pathv[i]);

	print_msg(JERS_LOG_INFO, "Loaded %ld jobs", jobFiles.gl_pathc);

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
	q->state = JERS_QUEUE_DEFAULT_STATE;
	q->obj.type = JERS_OBJECT_QUEUE;

	/* Read the contents and get the details */
	ssize_t len;
	while((len = getline(&line, &lineSize, f)) != -1) {
		char * key = NULL, *value = NULL;
		int index;

		if (loadKeyValue(line, &key, &value, &index))
			error_die("stateLoadQueue: Error parsing queue file: %s\n", fileName);

		if (!key || !value)
			continue;

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
		} else if (strcmp(key, "REVISION") == 0) {
			q->obj.revision = atol(value);
		} else {
			print_msg(JERS_LOG_WARNING, "stateLoadQueue: skipping unknown config '%s' for queue %s\n", key, name);
		}
	}

	if (len == -1 && feof(f) == 0)
		error_die("Error reading queue file %s: %s\n", fileName, strerror(errno));

	fclose(f);
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
	size_t i;
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

	print_msg(JERS_LOG_INFO, "Loading %ld queues from disk", qFiles.gl_pathc);

	for (i = 0; i < qFiles.gl_pathc; i++)
		stateLoadQueue(qFiles.gl_pathv[i]);

	print_msg(JERS_LOG_INFO, "Loaded %ld queues from disk", qFiles.gl_pathc);

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
	r->obj.type = JERS_OBJECT_RESOURCE;

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
		} else if (strcmp(key, "REVISION") == 0) {
			r->obj.revision = atol(value);
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
	size_t i;
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

	print_msg(JERS_LOG_INFO, "Loading %ld resources from disk", resFiles.gl_pathc);

	for (i = 0; i < resFiles.gl_pathc; i++)
		stateLoadRes(resFiles.gl_pathv[i]);

	print_msg(JERS_LOG_INFO, "Loaded %ld resources.", resFiles.gl_pathc);

	globfree(&resFiles);
	return 0;
}

static inline void decrement_state(struct job *j) {
	switch (j->state) {
		case JERS_JOB_RUNNING:
			server.stats.jobs.running--;
			j->queue->stats.running--;
			break;

		case JERS_JOB_PENDING:
			server.stats.jobs.pending--;
			j->queue->stats.pending--;
			break;

		case JERS_JOB_DEFERRED:
			server.stats.jobs.deferred--;
			j->queue->stats.deferred--;
			break;

		case JERS_JOB_HOLDING:
			server.stats.jobs.holding--;
			j->queue->stats.holding--;
			break;

		case JERS_JOB_COMPLETED:
			server.stats.jobs.completed--;
			j->queue->stats.completed--;
			break;

		case JERS_JOB_EXITED:
			server.stats.jobs.exited--;
			j->queue->stats.exited--;
			break;

		case JERS_JOB_UNKNOWN:
			server.stats.jobs.unknown--;
			j->queue->stats.unknown--;
			break;
	}
}

static inline void increment_state(struct job *j) {
	switch (j->state) {
		case JERS_JOB_RUNNING:
			server.stats.jobs.running++;
			j->queue->stats.running++;

			server.stats.jobs.start_pending--;
			j->queue->stats.start_pending--;
			break;

		case JERS_JOB_PENDING:
			server.stats.jobs.pending++;
			j->queue->stats.pending++;
			server.candidate_recalc = 1;
			break;

		case JERS_JOB_DEFERRED:
			server.stats.jobs.deferred++;
			j->queue->stats.deferred++;
			server.candidate_recalc = 1;
			break;

		case JERS_JOB_HOLDING:
			server.stats.jobs.holding++;
			j->queue->stats.holding++;
			server.candidate_recalc = 1;
			break;

		case JERS_JOB_COMPLETED:
			server.stats.jobs.completed++;
			j->queue->stats.completed++;
			break;

		case JERS_JOB_EXITED:
			server.stats.jobs.exited++;
			j->queue->stats.exited++;
			break;

		case JERS_JOB_UNKNOWN:
			server.stats.jobs.unknown++;
			j->queue->stats.unknown++;
	}
}

void changeJobState(struct job *j, int new_state, struct queue *new_queue, int dirty) {
	if (j->state != new_state || new_queue != NULL) {
		/* Update the job state and appropriate counts */
		decrement_state(j);

		if (new_queue != NULL)
			j->queue = new_queue;

		j->state = new_state;
		increment_state(j);
	}

	updateObject(&j->obj, dirty);
}

void updateObject(jers_object * obj, int dirty) {
	obj->revision++;

	if (dirty) {
		obj->dirty = 1;

		switch(obj->type) {
			case JERS_OBJECT_JOB: server.dirty_jobs = 1; break;
			case JERS_OBJECT_QUEUE: server.dirty_queues = 1; break;
			case JERS_OBJECT_RESOURCE: server.dirty_resources = 1; break;
		}
	}
}

void flush_journal(int force) {
	if (!force && !server.flush.dirty)
		return;

	fdatasync(server.journal.fd);
	server.flush.lastflush = time(NULL);
	server.flush.dirty = 0;
}
