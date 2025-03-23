#ifndef FIREWALL_H
#define FIREWALL_H

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

// Common firewall-allowed ports
#define FW_PORT_COUNT 10
extern const int FIREWALL_FRIENDLY_PORTS[FW_PORT_COUNT];

// Function prototypes
int try_firewall_friendly_ports(Node* node, int base_port);
int punch_multiple_ports(Node* from_node, NodeInfo* peer);

#endif /* FIREWALL_H */