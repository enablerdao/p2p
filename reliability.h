#ifndef RELIABILITY_H
#define RELIABILITY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "node.h"

// Reliability settings
#define RECONNECT_INTERVAL 30  // Seconds between reconnection attempts
#define MAX_RECONNECT_ATTEMPTS 5
#define KEEPALIVE_INTERVAL 15  // Seconds between keepalive messages

// Function prototypes
void send_keepalive(Node* node);
int reconnect_to_peer(Node* node, int peer_id);
void* reliability_thread(void* arg);
int start_reliability_service(Node* node);
void stop_reliability_service(Node* node);

#endif /* RELIABILITY_H */