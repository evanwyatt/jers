JERS_CFLAGS=$(CFLAGS) -g -fPIC -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -std=c99 -D_GNU_SOURCE -fvisibility=hidden
JERS_LDFLAGS=$(LD_FLAGS) -rdynamic -lsystemd -lcrypto

INC=-I../src -I../deps -I./
COMMON_OBJS=../src/common.o ../src/fields.o ../src/json.o ../src/buffer.o ../src/logging.o ../src/state.o ../src/jobs.o ../src/queue.o ../src/resource.o ../src/commands.o ../src/command_job.o ../src/command_queue.o
COMMON_OBJS+= ../src/command_resource.o ../src/command_agent.o ../src/setproctitle.o ../src/email.o ../src/client.o ../src/agent.o ../src/comms.o ../src/error.o ../src/auth.o

SRCFILES := $(shell find ./ -type f -name "test_*.c")
TEST_CASES := $(patsubst %.c,%.o,$(SRCFILES))

test: run_tests

run_tests: run_tests.o $(TEST_CASES)
	$(CC) $(JERS_LDFLAGS) $(COMMON_OBJS) -o $@ $^

%.o: %.c
	$(CC) $(JERS_CFLAGS) -c $(INC) $<

clean:
	rm -rf run_tests *.o 