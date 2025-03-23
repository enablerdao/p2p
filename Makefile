CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -pthread -lcrypto

# macOS specific settings
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
        # On macOS, pthread is part of libc
        CFLAGS = -Wall -Wextra
        # Check if OpenSSL is installed via Homebrew
        ifneq ($(wildcard /usr/local/opt/openssl@3),)
                CFLAGS += -I/usr/local/opt/openssl@3/include
                LDFLAGS = -L/usr/local/opt/openssl@3/lib -lcrypto
        else
                LDFLAGS = -lcrypto
        endif
endif

SRCS = main.c node.c stun.c upnp.c discovery.c discovery_server.c enhanced_discovery.c nat_traversal.c firewall.c reliability.c security.c diagnostics.c dht.c rendezvous.c turn.c ice.c
OBJS = $(SRCS:.c=.o)
HDRS = node.h stun.h upnp.h discovery.h discovery_server.h enhanced_discovery.h firewall.h reliability.h security.h diagnostics.h dht.h rendezvous.h turn.h ice.h

all: node_network

node_network: $(OBJS)
        $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HDRS)
        $(CC) $(CFLAGS) -c $<

clean:
        rm -f node_network *.o

.PHONY: all clean