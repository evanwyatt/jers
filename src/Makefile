JERS_CFLAGS=$(CFLAGS) -g -fPIC -pedantic  -Wno-missing-field-initializers -std=c99 -D_GNU_SOURCE #-Wall -W
JERS_LDFLAGS=$(LD_FLAGS) -rdynamic

INC=-I./ -I ../deps/

ifeq ($(DESTDIR),)
	DESTDIR := /usr/local
endif

JERSD_OBJS=jersd.o error.o config.o event.o resp.o commands.o state.o jobs.o auth.o comms.o sched.o common.o queue.o buffer.o queue.o fields.o resource.o command_job.o command_agent.o command_queue.o command_resource.o logging.o
JERSAGENTD_OBJS=jers_agentd.o common.o error.o resp.o buffer.o fields.o logging.o error.o
JERS_OBJS=jers.o
JERS_DUMP_OBJS=jers_dump_env.o
LIBJERS_OBJS=api.o resp.o fields.o buffer.o common.o error.o

all: jersd jers_agentd jers_dump_env jers

jersd: $(JERSD_OBJS)
	$(CC) $(JERS_LDFLAGS) -o $@ $^

jers_agentd: $(JERSAGENTD_OBJS)
	$(CC) $(JERS_LDFLAGS) -o $@ $^

libjers.so: $(LIBJERS_OBJS)
	$(CC) $(JERS_LDFLAGS) -shared -Wl,-soname,libjers.so -o $@ $^

jers: $(JERS_OBJS) libjers.so
	$(CC) $(JERS_LDFLAGS) $(JERS_OBJS) -L. -ljers -o $@

jers_dump_env: $(JERS_DUMP_OBJS)

%.o: %.c
	$(CC) $(JERS_CFLAGS) -c $(INC) $<

install: libjers.so jersd jers_agentd jers

	install -m 644 libjers.so $(DESTDIR)/lib64/
	install -m 644 jers.h $(DESTDIR)/include/
	install -m 755 jersd jers_agentd jers jers_dump_env $(DESTDIR)/bin
	
clean:
	rm -rf jersd jers jers_agentd *.o *.so

