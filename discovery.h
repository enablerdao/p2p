#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include "node.h"

#define DISCOVERY_PORT 8888
#define DISCOVERY_MULTICAST_ADDR "239.255.255.250"
#define DISCOVERY_INTERVAL 10 // seconds
#define DISCOVERY_TIMEOUT 1 // seconds

// Function prototypes
int discovery_init(Node* node);
void discovery_announce(Node* node);
void discovery_listen(Node* node);
void discovery_cleanup();
void* discovery_thread(void* arg);

#endif /* DISCOVERY_H */