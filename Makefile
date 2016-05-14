CC=gcc
LIBS=-ltermcap
CFLAGS=-O2 -g -Wall -DUNIX98
LDFLAGS=

TARGET=pefop
OBJS=pefop.o server.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -rf $(TARGET) $(OBJS)

pefop.o: Makefile pefop.c

server.o: Makefile server.c server.h

install:
	install -s $(TARGET) /usr/local/bin/
