CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -pthread

SRCS = main.c node.c stun.c upnp.c discovery.c nat_traversal.c
OBJS = $(SRCS:.c=.o)
HDRS = node.h stun.h upnp.h discovery.h

all: node_network

node_network: $(OBJS)
        $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HDRS)
        $(CC) $(CFLAGS) -c $<

clean:
        rm -f node_network *.o

.PHONY: all clean