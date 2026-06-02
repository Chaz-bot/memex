CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS =
LIBS = -lncurses

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: memex

memex: memex.c memex_config.h platform.h platform_posix.c ui_curses.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o memex memex.c platform_posix.c $(LIBS)

install: memex
	install -d $(BINDIR)
	install -m 755 memex $(BINDIR)/memex

smoke: memex
	rm -rf /tmp/memex-smoke
	./memex --smoke-test /tmp/memex-smoke

persistence: memex
	rm -rf /tmp/memex-persistence
	./memex --persistence-test /tmp/memex-persistence

performance: memex
	rm -rf /tmp/memex-performance
	./memex --performance-test /tmp/memex-performance

triage:
	$(CC) $(CFLAGS) -DMEMEX_DISABLE_SAVED_SEARCHES -DMEMEX_DISABLE_MENTIONS -DMEMEX_DISABLE_TRANSCLUSION -DMEMEX_DISABLE_MOUSE $(LDFLAGS) -o memex memex.c platform_posix.c $(LIBS)
	rm -rf /tmp/memex-triage
	rm -rf /tmp/memex-triage-persistence
	./memex --smoke-test /tmp/memex-triage
	./memex --persistence-test /tmp/memex-triage-persistence

clean:
	rm -f memex *.o core

.PHONY: all install smoke persistence performance triage clean
