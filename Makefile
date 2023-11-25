CC=gcc
##LIBS=-ltermcap
LIBS=-lncursesw
CFLAGS=-O2 -g -Wall -DUNIX98
LDFLAGS=

TARGET=pefop
OBJS=pefop.o

TARGET-UTF8=pefop-utf8
OBJS-UTF8=pefop-utf8.o

COMMON_OBJS=server.o

all: $(TARGET) $(TARGET-UTF8)

$(TARGET): $(OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(COMMON_OBJS) $(LDFLAGS) $(LIBS)

$(TARGET-UTF8): $(OBJS-UTF8) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS-UTF8) $(COMMON_OBJS) $(LDFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -rf $(TARGET) $(OBJS) $(TARGET-UTF8) $(OBJS-UTF8) $(COMMON_OBJS)

pefop.o: Makefile pefop.c

pefop-utf8.o: Makefile pefop-utf8.c

server.o: Makefile server.c server.h

install:
	install -s $(TARGET) /usr/local/bin/
	install -s $(TARGET-UTF8) /usr/local/bin/
