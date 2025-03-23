CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -pthread

# macOS specific settings
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	# On macOS, pthread is part of libc
	CFLAGS = -Wall -Wextra
	LDFLAGS = 
endif

SRCS = main.c node.c stun.c upnp.c discovery.c discovery_server.c enhanced_discovery.c nat_traversal.c firewall.c reliability.c security.c diagnostics.c
OBJS = $(SRCS:.c=.o)
HDRS = node.h stun.h upnp.h discovery.h discovery_server.h enhanced_discovery.h firewall.h reliability.h security.h diagnostics.h

all: node_network

node_network: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f node_network *.o

.PHONY: all clean