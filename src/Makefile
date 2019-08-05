JERS_CFLAGS=$(CFLAGS) -g -fPIC -Wall -Wextra -Wpedantic  -Wno-missing-field-initializers -std=c99 -D_GNU_SOURCE -fvisibility=hidden
JERS_LDFLAGS=$(LD_FLAGS) -rdynamic

EXTERNAL_LIBS=-lcrypto -lssl

INC=-I./ -I ../deps/

ifeq ($(DESTDIR),)
	DESTDIR := /usr/local
endif

# Extract the version number
JERS_MAJOR=$(shell grep JERS_MAJOR jers.h | awk '{print $$3}')
JERS_MINOR=$(shell grep JERS_MINOR jers.h | awk '{print $$3}')
JERS_PATCH=$(shell grep JERS_PATCH jers.h | awk '{print $$3}')


JERSD_OBJS=jersd.o error.o config.o event.o resp.o commands.o state.o jobs.o auth.o comms.o sched.o common.o queue.o buffer.o queue.o fields.o resource.o command_job.o command_agent.o command_queue.o command_resource.o logging.o setproctitle.o
JERSAGENTD_OBJS=jers_agentd.o common.o error.o resp.o buffer.o fields.o logging.o error.o setproctitle.o auth.o
JERS_OBJS=jers.o jers_cli.o common.o

JERS_DUMP_OBJS=jers_dump_env.o
LIBJERS_OBJS=api.o resp.o fields.o buffer.o common.o error.o

all: jersd jers_agentd jers_dump_env jers

jersd: $(JERSD_OBJS)
	$(CC) $(JERS_LDFLAGS) -o $@ $^ $(EXTERNAL_LIBS)

jers_agentd: $(JERSAGENTD_OBJS)
	$(CC) $(JERS_LDFLAGS) -o $@ $^ $(EXTERNAL_LIBS)

libjers.so: $(LIBJERS_OBJS)
	$(CC) $(JERS_LDFLAGS) -shared -Wl,-soname,libjers.so.$(JERS_MAJOR) -o $@ $^ $(EXTERNAL_LIBS)

jers: $(JERS_OBJS) libjers.so
	$(CC) $(JERS_LDFLAGS) $(JERS_OBJS) -L. -ljers -o $@

jers_dump_env: $(JERS_DUMP_OBJS)

%.o: %.c
	$(CC) $(JERS_CFLAGS) -c $(INC) $<

install: libjers.so jersd jers_agentd jers

	install -m 644 libjers.so $(DESTDIR)/lib64/libjers.so.$(JERS_MAJOR).$(JERS_MINOR).$(JERS_PATCH)
	install -m 644 jers.h $(DESTDIR)/include/
	install -m 755 jersd jers_agentd jers jers_dump_env $(DESTDIR)/bin

	cd $(DESTDIR)/lib64/ && ln -sf libjers.so.$(JERS_MAJOR).$(JERS_MINOR).$(JERS_PATCH) libjers.so.$(JERS_MAJOR)
	cd $(DESTDIR)/lib64/ && ln -sf libjers.so.$(JERS_MAJOR) libjers.so

clean:
	rm -rf jersd jers jers_agentd *.o *.so

