#ifndef DISCOVERY_SERVER_H
#define DISCOVERY_SERVER_H

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

// Discovery server settings
#define DEFAULT_DISCOVERY_SERVER "discovery.p2pnetwork.example.com"
#define DEFAULT_DISCOVERY_PORT 8888

// Function prototypes
int register_with_discovery_server(Node* node, const char* server, int port);
int query_discovery_server(Node* node, const char* server, int port);
void* discovery_server_thread(void* arg);
int start_discovery_server_client(Node* node, const char* server, int port);
void stop_discovery_server_client(Node* node);

#endif /* DISCOVERY_SERVER_H */