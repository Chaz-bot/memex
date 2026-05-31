CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS =
LIBS = -lncurses

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: memex

memex: memex.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o memex memex.c $(LIBS)

install: memex
	install -d $(BINDIR)
	install -m 755 memex $(BINDIR)/memex

clean:
	rm -f memex *.o core

.PHONY: all install clean
