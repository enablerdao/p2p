CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -pthread

all: node_network

node_network: main.o node.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

main.o: main.c node.h
	$(CC) $(CFLAGS) -c $<

node.o: node.c node.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f node_network *.o

.PHONY: all clean