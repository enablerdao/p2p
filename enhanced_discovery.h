#ifndef ENHANCED_DISCOVERY_H
#define ENHANCED_DISCOVERY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <ifaddrs.h>
#include <net/if.h>
#include "node.h"

// Enhanced discovery settings
#define ENHANCED_DISCOVERY_PORT 8889
#define ENHANCED_MULTICAST_ADDR "239.255.255.251"
#define ENHANCED_DISCOVERY_INTERVAL 5 // seconds
#define ENHANCED_DISCOVERY_TIMEOUT 1 // seconds
#define ENHANCED_DISCOVERY_TTL 32 // Time-to-live for multicast packets

// Discovery message types
#define DISC_MSG_ANNOUNCE 1
#define DISC_MSG_QUERY 2
#define DISC_MSG_RESPONSE 3

// Discovery message structure
typedef struct {
    uint8_t type;              // Message type
    int node_id;               // Node ID
    char ip[MAX_IP_STR_LEN];   // IP address
    int port;                  // Port
    char public_ip[MAX_IP_STR_LEN]; // Public IP (if behind NAT)
    int public_port;           // Public port (if behind NAT)
    bool is_public;            // Whether node is publicly accessible
    uint32_t timestamp;        // Timestamp for freshness
    uint32_t sequence;         // Sequence number for deduplication
} EnhancedDiscoveryMessage;

// Function prototypes
int enhanced_discovery_init(Node* node);
int enhanced_discovery_send_announcement(Node* node);
int enhanced_discovery_send_query(Node* node);
int enhanced_discovery_process_message(Node* node, EnhancedDiscoveryMessage* msg, struct sockaddr_in* sender_addr);
void* enhanced_discovery_thread(void* arg);
void enhanced_discovery_cleanup();

#endif /* ENHANCED_DISCOVERY_H */