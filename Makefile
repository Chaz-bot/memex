CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS =
LIBS = -lncurses

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: memex

memex: memex.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o memex memex.c $(LIBS)

# 32-bit static i386 binary for deployment to old Linux machines.
# Requires: gcc-multilib lib32ncurses-dev (Ubuntu: sudo apt install gcc-multilib lib32ncurses-dev)
# NOTE: glibc static binaries require Linux 3.2+; to run on kernel 2.2.x
#       (e.g. Slackware 4.0), compile directly on the target machine instead.
memex32: memex.c
	$(CC) -m32 -static -O2 -Wall -o memex32 memex.c -lncurses -ltinfo
	strip memex32

install: memex
	install -d $(BINDIR)
	install -m 755 memex $(BINDIR)/memex

clean:
	rm -f memex memex32 *.o core

.PHONY: all memex32 install clean
