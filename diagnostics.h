#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "node.h"

// Diagnostic settings
#define PING_TIMEOUT 5  // Seconds to wait for ping response

// Function prototypes
void print_node_status(Node* node);
void print_peer_status(Node* node);
int ping_peer(Node* node, int peer_id, int timeout_sec);
void run_network_diagnostics(Node* node);
void log_network_event(Node* node, const char* event, const char* details);

#endif /* DIAGNOSTICS_H */