#include <stdio.h>
#include <errno.h>
#include <limits.h>

#include <jers_tests.h>
#include <server.h>

void stateInit(void);
int stateSaveJob(struct job *j);
int stateSaveQueue(struct queue *q);
int stateSaveResource(struct resource *r);

//struct jersServer server = {0};

#define CMP_INT(__n)                                  \
	if (a->__n != b->__n) {                           \
		printf("Field '%s' does not match.\n", #__n); \
		return 1;                                     \
	}
#define CMP_STR(__n)                                                                    \
	if (cmp_str(a->__n, b->__n)) {                                                      \
		printf("Field '%s' does not match. a = '%s' b = '%s'\n", #__n, a->__n, b->__n); \
		return 1;                                                                       \
	}
#define CMP_STRARRAY(__n, __c)                                            \
	do {                                                                  \
		if (a->__c != b->__c || cmp_strarray(a->__c, a->__n, b->__n)) {   \
			printf("Field '%s' does not match\n", #__n);                  \
			return 1;                                                     \
		}                                                                 \
	} while (0);

static int cmp_str(const char *a, const char *b) {
	if ((a == NULL && b != NULL) || (b == NULL && a != NULL))
		return 1;

	if (a == NULL && b == NULL)
		return 0;

	return strcmp(a, b);
}

static int cmp_strarray(int count, char **a, char **b)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(a[i], b[i]) != 0) {
			printf("[%d] '%s' != '%s'\n", i, a[i], b[i]);
			return 1;
		}
	}

	return 0;
}

int cmp_resource(const struct resource *a, const struct resource *b) {
	CMP_INT(obj.type);
	CMP_INT(obj.revision);

	CMP_STR(name);
	CMP_INT(count);

	return 0;
}

int cmp_queue(const struct queue *a, const struct queue *b) {
	CMP_INT(obj.type);
	CMP_INT(obj.revision);

	CMP_STR(name);
	CMP_STR(desc);
	CMP_INT(job_limit);
	CMP_INT(priority);
	CMP_INT(nice);
	CMP_STR(host);
	CMP_INT(def);

	return 0;
}

/* Compare two struct job to see if they are functionaly the same */
int cmp_job(const struct job *a, const struct job *b) {
	CMP_INT(obj.type);
	CMP_INT(obj.revision);
	CMP_INT(jobid);
	CMP_STR(jobname);
	CMP_INT(queue);
	CMP_STR(shell);
	CMP_STR(wrapper);
	CMP_STR(pre_cmd);
	CMP_STR(post_cmd);
	CMP_STR(stdout);
	CMP_STR(stderr);
	CMP_STRARRAY(argv, argc);
	CMP_STRARRAY(envs, env_count);
	CMP_INT(uid);
	CMP_INT(submitter);
	CMP_INT(nice);
	CMP_INT(pid);
	CMP_INT(exitcode);
	CMP_INT(signal);
	CMP_INT(email_states);
	CMP_STR(email_addresses);

	if (memcmp(&a->usage, &b->usage, sizeof(struct rusage)) != 0) {
		printf("Field '%s' does not match\n", "usage");
		return 1;
	}

	CMP_INT(state);
	CMP_INT(pend_reason);
	CMP_INT(fail_reason);

	CMP_INT(priority);
	CMP_INT(submit_time);
	CMP_INT(start_time);
	CMP_INT(defer_time);
	CMP_INT(finish_time);

	CMP_INT(tag_count);

	for (int i = 0; i < a->tag_count; i++) {
		if (strcmp(a->tags[i].key, b->tags[i].key) != 0) {
			printf("Field '%s' does not match\n", "tag - key");
			return 1;
		}

		if (strcmp(a->tags[i].value, a->tags[i].value) != 0) {
			printf("Field '%s' does not match\n", "tag - value");
			return 1;
		}
	}

	CMP_INT(res_count);

	for (int i = 0; i < a->res_count; i++) {
		if (memcmp(a->req_resources, b->req_resources, sizeof(struct jobResource)) != 0)
			return 1;
	}

	CMP_INT(internal_state);

	return 0;
}

static int test_job_state(struct job *j) {
	struct job *new = NULL;
	char filename[PATH_MAX];

	/* Create a state file for job pointed to by j */
	if (stateSaveJob(j) != 0) {
		DEBUG("Failed to save job");
		return 1;
	}

	int directory = j->jobid / STATE_DIV_FACTOR;
	sprintf(filename, "%s/jobs/%d/%d.job", server.state_dir, directory, j->jobid);

	/* Load it back up */
	new = stateLoadJob(filename);

	if (new == NULL) {
		DEBUG("Failed to load job");
		return 1;
	}

	/* Compare it */
	int status = cmp_job(j, new);
	freeJob(new);
	unlink(filename);

	return status;
}

static void test_job_states(void) {
	struct job j = {0};
	char *args[20];
	char *envs[10];

	struct queue *q = calloc(1, sizeof(struct queue));
	q->name = "test_queue_1"; //Minimum needed to save a job
	HASH_ADD_STR(server.queueTable, name, q);

	j.jobid = 1234;
	j.obj.type = JERS_OBJECT_JOB;
	j.obj.revision = 1;
	j.jobname = "Test state test";
	j.argc = 2;
	args[0] = "echo";
	args[1] = "Hello World.";
	j.argv = args;
	j.submit_time = time(NULL);
	j.state = JERS_JOB_HOLDING;
	j.submitter = getuid();
	j.queue = q;

	TEST("State{Save/load}Job - Minimal test", test_job_state(&j) != 0);

	/* Most fields populated */
	memset(&j, 0, sizeof(struct job));
	j.jobid = 2468;
	j.obj.type = JERS_OBJECT_JOB;
	j.obj.revision = 10;
	j.jobname = "Longer job name #123123123132";
	j.argc = 5;
	j.argv = args;
	args[0] = "echo";
	args[1] = "one";
	args[2] = "t\tw\to";
	args[3] = "";
	args[4] = "    four";

	j.submit_time = time(NULL);
	j.defer_time = time(NULL) + 100;
	j.state = JERS_JOB_DEFERRED;

	j.submitter = getuid();
	j.queue = q;
	j.shell = "/bin/bash";
	j.pre_cmd = "echo \"PRE\tCMD\"";
	j.post_cmd = "echo post cmd ";

	j.stdout = "/tmp/test_stdout.log";
	j.stderr = "/tmp/test_stderr.log";

	j.env_count = 2;
	j.envs = envs;
	envs[0] = "DEBUG=Y";
	envs[1] = "ENV=VAR";

	j.nice = 10;
	j.priority = 5;

	TEST("State{Save/load}Job - Deferred test", test_job_state(&j) != 0);

	memset(&j, 0, sizeof(struct job));
	j.jobid = 9999;
	j.obj.type = JERS_OBJECT_JOB;
	j.obj.revision = 5;
	j.jobname = "Exited job";
	j.argc = 2;
	j.argv = args;
	args[0] = "echo";
	args[1] = "#one";

	j.submit_time = time(NULL);
	j.state = JERS_JOB_EXITED;
	j.exitcode = 128 + SIGSEGV;
	j.signal = SIGSEGV;

	j.submitter = getuid();
	j.queue = q;
	j.shell = "/bin/bash";

	j.stdout = "/tmp/test_stdout.log";
	j.stderr = "/tmp/test_stderr.log";

	j.env_count = 2;
	j.envs = envs;
	envs[0] = "DEBUG=Y";
	envs[1] = "ENV=VAR";

	j.nice = 10;
	j.priority = 5;

	TEST("State{Save/load}Job - Exited job", test_job_state(&j) != 0);

	/* Remove our dummy queue */
	HASH_DEL(server.queueTable, q);
	free(q);
	server.queueTable = NULL;
}

int test_queue_state(struct queue *q) {
	struct queue *new = NULL;
	char filename[PATH_MAX];

	/* Create a state file for job pointed to by j */
	if (stateSaveQueue(q) != 0) {
		DEBUG("Failed to save queue");
		return 1;
	}
	sprintf(filename, "%s/queues/%s.queue", server.state_dir, q->name);

	/* Load it back up */
	new = stateLoadQueue(filename);

	if (new == NULL) {
		DEBUG("Failed to load job");
		return 1;
	}

	/* Compare it */
	int status = cmp_queue(q, new);
	freeQueue(new);
	unlink(filename);

	return status;
}

void test_queue_states(void) {
	struct queue q = {0};

	q.obj.type = JERS_OBJECT_QUEUE;
	q.obj.revision = 2;
	q.name = "test_queue1";
	q.job_limit = 1;
	q.desc = "#Description ";
	q.host = "localhost";
	q.priority = 2;

	q.def = 1;
	server.defaultQueue = &q;

	TEST("state{Save/Load}Queue - Default queue", test_queue_state(&q));

	memset(&q, 0, sizeof(struct queue));
	q.obj.type = JERS_OBJECT_QUEUE;
	q.obj.revision = 10;
	q.name = "test queue";
	q.job_limit = 100;
	q.desc = "";
	q.host = "host1.fqdn.net";
	q.priority = 1;

	server.defaultQueue = NULL;

	TEST("state{Save/Load}Queue - Non default", test_queue_state(&q));
}

int test_resource_state(struct resource *r) {
	struct resource *new = NULL;
	char filename[PATH_MAX];

	/* Create a state file for job pointed to by j */
	if (stateSaveResource(r) != 0) {
		DEBUG("Failed to save resource");
		return 1;
	}
	sprintf(filename, "%s/resources/%s.resource", server.state_dir, r->name);

	/* Load it back up */
	new = stateLoadResource(filename);

	if (new == NULL) {
		DEBUG("Failed to load job");
		return 1;
	}

	/* Compare it */
	int status = cmp_resource(r, new);
	freeRes(new);
	unlink(filename);

	return status;
}

void test_resource_states(void) {
	struct resource r = {0};
	r.obj.type = JERS_OBJECT_RESOURCE;
	r.obj.revision = 2;
	r.name = "test_resource";
	r.count = 10;

	TEST("state{Save/Load}Resource", test_resource_state(&r));

	memset(&r, 0, sizeof(struct resource));
	r.obj.type = JERS_OBJECT_RESOURCE;
	r.obj.revision = 10;
	r.name = "test_resource2";
	r.count = 100;

	TEST("state{Save/Load}Resource", test_resource_state(&r));
}

/* Test the saving and loading of state files */

void test_state(void) {
	/* Create a temporary directory to save this to */
	char *tmpdir = getenv("TMPDIR");
	char temp_dir[PATH_MAX];

	sprintf(temp_dir, "%s/XXXXXX", tmpdir ? tmpdir : "/tmp");
	if (mkdtemp(temp_dir) == NULL || *temp_dir == '\0') {
		printf("Failed to create temporary directory for state tests: %s\n", strerror(errno));
		exit(1);
	}

	server.state_dir = temp_dir;
	stateInit();

	test_job_states();
	test_queue_states();
	test_resource_states();
}