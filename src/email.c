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

#include <sys/wait.h>

#include "server.h"
#include "jers.h"
#include "common.h"

#include "email.h"

#define MAX_EMAIL_PIDS 5
#define EMAIL_BINARY "/usr/bin/mailx"

int disable_email = 0;

email *emailList = NULL;
email *emailTail = NULL;

/* Adds an email to the tail */
void addEmail(email *e) {
    if (emailList == NULL) {
        emailList = emailTail = e;
        return;
    }

    /* Already have a list */
    emailTail->next = e;
    e->prev = emailTail;
    emailTail = e;
}

void removeEmail(email *e) {
	if (e->next)
		e->next->prev = e->prev;

	if (e->prev)
		e->prev->next = e->next;

	if (e == emailList)
		emailList = e->next;

    if (e == emailTail)
        emailTail = e->prev;
}

void freeEmail(email *e) {
    free(e->to);
    free(e->subject);
    free(e->content);
    free(e);
}

static char *emailState(int state) {
    char *str = "UNKNOWN";

    switch (state) {
		case JERS_JOB_COMPLETED: str = "completed"; break;
		case JERS_JOB_EXITED:    str = "exited"; break;
		case JERS_JOB_HOLDING:   str = "holding"; break;
		case JERS_JOB_DEFERRED:  str = "deferred"; break;
		case JERS_JOB_PENDING:   str = "pending"; break;
		case JERS_JOB_RUNNING:   str = "running"; break;
		case JERS_JOB_UNKNOWN:   str = "unknown"; break;
    }

    return str;
}

void generateEmail(struct job *j, int new_state) {
    /* Don't send emails if we are recovering or starting up */
    if (unlikely(server.recovery.in_progress || server.initalising))
        return;

    email *e = calloc(sizeof(email), 1);
    struct timespec now;

    if (e == NULL) {
        print_msg(JERS_LOG_WARNING, "Failed to generate email for job %d: %s", j->jobid, strerror(errno));
        return;
    }

    clock_gettime(CLOCK_REALTIME_COARSE, &now);

    asprintf(&e->subject, "JERS job %u now %s", j->jobid, emailState(new_state));
    asprintf(&e->content, "Email generated from %s at %s\n", gethost(), print_time(&now, 0));
    e->to = strdup(j->email_addresses);

    addEmail(e);
}

void sendEmails(void) {
    /* For any pid marked as -1 in the list, attempt to send an email */
    for (email *e = emailList; e; e = e->next) {
        int fd[2];

        if (pipe(fd) == -1) {
            print_msg(JERS_LOG_WARNING, "Failed to create pipe for email messages: %s", strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        pid_t pid = fork();

        if (pid == -1) {
            print_msg(JERS_LOG_WARNING, "Failed to fork during email sending: %s", strerror(errno));
            _exit(EXIT_FAILURE);
        }

        if (pid) {
            int status = 0;
            close(fd[0]); // Read end
            write(fd[1], e->content, strlen(e->content));
            close(fd[1]);

            if (waitpid(pid, &status, 0) < 0) {
                print_msg(JERS_LOG_WARNING, "Failed to send email (waitpid failed): %s", strerror(errno));
                _exit(EXIT_FAILURE);
            }

            if (status) {
                print_msg(JERS_LOG_WARNING, "Failed to send email (bad status): %s", strerror(errno));
                _exit(EXIT_FAILURE);
            }
        } else {
            char *args[5];
            args[0] = EMAIL_BINARY;
            args[1] = "-s";
            args[2] = e->subject;
            args[3] = e->to;
            args[4] = NULL;

            /* Redirect the pipe to stdin */
            close(fd[1]); // Write end
            dup2(fd[0], STDIN_FILENO);

            execv(args[0], args);
            print_msg(JERS_LOG_WARNING, "Failed to execv to send email: %s", strerror(errno));
            _exit(EXIT_FAILURE);
        }

    }

    _exit(EXIT_SUCCESS);
}

/* Check the existing processes, or spawn additional ones */
void checkEmailProcesses(void) {
    static pid_t pids[MAX_EMAIL_PIDS] = {0};
    int spawn = -1;
    int emails = 0;

    if (disable_email)
        return;

    if (access(EMAIL_BINARY, X_OK) != 0) {
        print_msg(JERS_LOG_WARNING, "Disabling email sending. - Email binary '%s' is not executable\n", EMAIL_BINARY);
        disable_email = 1;
        return;
    }

    /* Check the existing processes first */
    for (int i = 0; i < MAX_EMAIL_PIDS; i++) {
        if (pids[i] == 0) {
            spawn = i;
            continue;
        }

        pid_t p = 0;
        int status = 0;

        if ((p = waitpid(pids[i], &status, WNOHANG)) > 0) {
            if (status) {
                /* Potentially failed to send an email
                 * Find the emails assigned to the subprocess and mark them to try again */
                for (email *e = emailList; e; e = e->next) {
                    if (e->pid == p)
                        e->pid = 0;
                }
            } else {
                /* Completed, remove the emails assigned to this process */
                email *e = emailList;

                while (e) {
                    email *next = e->next;

                    if (e->pid == p) {
                        removeEmail(e);
                        freeEmail(e);
                    }

                    e = next;
                }
            }

            pids[i] = 0;
        }

        if (p == -1 && errno != ECHILD)
		    fprintf(stderr, "waitpid failed checking for email subprocess pid %d? %s\n", pids[i], strerror(errno));
    }

    /* Spawn a new process if we can/is needed */

    if (spawn < 0)
        return;

    for (email *e = emailList; e; e = e->next) {
        if (e->pid != 0)
            continue;

        /* Put a placeholder pid on this email */
        e->pid = -1;
        emails++;
    }

    if (emails == 0)
        return;

    /* Spawn a new process */
    pid_t email_process = fork();

    if (email_process == -1) {
        print_msg(JERS_LOG_WARNING, "Failed to fork() email process: %s", strerror(errno));
        for (email *e = emailList; e; e = e->next) {
            if (e->pid == -1)
                e->pid = 0;
        }

        return;
    }

    if (email_process == 0) {
        sendEmails();
        /* Does not return */
    }

    for (email *e = emailList; e; e = e->next) {
        if (e->pid == -1)
            e->pid = email_process;
    }

    pids[spawn] = email_process;

    return;
}