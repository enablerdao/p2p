#ifndef STUN_H
#define STUN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define STUN_PORT 3478
#define STUN_HEADER_SIZE 20
#define STUN_MAGIC_COOKIE 0x2112A442
#define STUN_BINDING_REQUEST 0x0001
#define STUN_BINDING_RESPONSE 0x0101
#define STUN_ATTR_MAPPED_ADDRESS 0x0001
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020

typedef struct {
    uint16_t message_type;
    uint16_t message_length;
    uint32_t magic_cookie;
    uint8_t transaction_id[12];
} StunHeader;

typedef struct {
    char public_ip[INET6_ADDRSTRLEN];
    int public_port;
} StunResult;

// Function prototypes
int stun_init();
StunResult* stun_discover_nat(const char* stun_server);
void stun_cleanup();

#endif /* STUN_H */