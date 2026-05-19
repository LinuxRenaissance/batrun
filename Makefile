CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11 -D_GNU_SOURCE
LDFLAGS ?=
LDLIBS  ?= -lsqlite3

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin

SRCS = src/batrun.c src/power_supply.c src/db.c \
       src/cmd_event.c src/cmd_report.c src/cmd_status.c
OBJS = $(SRCS:.c=.o)

all: batrun

batrun: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: batrun
	install -D -m 0755 batrun $(DESTDIR)$(BINDIR)/batrun

clean:
	rm -f batrun $(OBJS)

.PHONY: all install clean
