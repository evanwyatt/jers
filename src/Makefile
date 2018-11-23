CFLAGS= -g -Wall -fPIC
#LDFLAGS= -shared

INC=-I./ -I ../deps/

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif

JERSD_OBJS=jersd.o error.o config.o event.o resp.o commands.o state.o jobs.o auth.o comms.o sched.o common.o queue.o buffer.o queue.o fields.o resource.o command_job.o command_queue.o command_resource.o
JERSAGENTD_OBJS=jers_agentd.o common.o error.o resp.o buffer.o fields.o
JERS_OBJS=jers.o
LIBJERS_OBJS=api.o resp.o fields.o buffer.o 

all: jersd jers_agentd jers

jersd: $(JERSD_OBJS)

jers_agentd: $(JERSAGENTD_OBJS)

libjers.so: $(LIBJERS_OBJS)
	$(CC) ${LDFLAGS} -shared -Wl,-soname,libjers.so -o $@ $^

jers: $(JERS_OBJS) libjers.so
	$(CC) $(JERS_OBJS) -L. -ljers -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $(INC) $<

install: libjers.so jersd jers_agentd jers

	install -d $(PREFIX)/lib64/jers
	install -m 644 libjers.so $(PREFIX)/lib64/jers

	install -m 644 jers.h $(PREFIX)/include/
	install -m 755 jersd jers_agentd jers $(PREFIX)/bin
	
clean:
	rm -rf jersd jers jers_agentd *.o *.so

