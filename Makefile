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

clean:
	rm -f memex *.o core

.PHONY: all install smoke persistence performance clean
