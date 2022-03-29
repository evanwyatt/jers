JERS_CFLAGS=$(CFLAGS) -g -fPIC -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -std=c11 -D_GNU_SOURCE -fvisibility=hidden
JERS_LDFLAGS=$(LD_FLAGS) -rdynamic -lsystemd -lcrypto

EXTERNAL_LIBS=-lcrypto -lssl

ifeq ($(USE_SYSTEMD),)
	EXTERNAL_LIBS+=-lsystemd
	JERS_CFLAGS+= -DUSE_SYSTEMD
endif

INC=-I../src -I../deps -I./
COMMON_OBJS=../src/common.o ../src/fields.o ../src/json.o ../src/buffer.o ../src/logging.o ../src/state.o ../src/jobs.o ../src/queue.o ../src/resource.o ../src/commands.o ../src/command_job.o ../src/command_queue.o
COMMON_OBJS+= ../src/command_resource.o ../src/command_agent.o ../src/setproctitle.o ../src/email.o ../src/client.o ../src/agent.o ../src/comms.o ../src/error.o ../src/auth.o ../src/sched.o ../src/tags.o

SRCFILES := $(shell find ./ -type f -name "test_*.c")
TEST_CASES := $(patsubst %.c,%.o,$(SRCFILES))

test: run_tests

run_tests: run_tests.o $(TEST_CASES)
	$(CC) $(JERS_LDFLAGS) $(COMMON_OBJS) $(EXTERNAL_LIBS) -o $@ $^

%.o: %.c
	$(CC) $(JERS_CFLAGS) -c $(INC) $<

clean:
	rm -rf run_tests *.o
