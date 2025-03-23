#ifndef NODE_H
#define NODE_H

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
#include <netdb.h>
#include <time.h>

#define MAX_NODES 100
#define MAX_BUFFER 1024
#define BASE_PORT 8000
#define MAX_IP_STR_LEN 40  // Support for IPv6 addresses

// Forward declaration for circular dependencies
struct Node;

// Structure to hold node connection information
typedef struct {
    int id;                     // Node ID
    char ip[MAX_IP_STR_LEN];    // IP address as string
    int port;                   // Port number
    time_t last_seen;           // Last time this peer was seen
    bool is_public;             // Whether this peer is publicly accessible
    char public_ip[MAX_IP_STR_LEN]; // Public IP address (if behind NAT)
    int public_port;            // Public port (if behind NAT)
} NodeInfo;

typedef struct Node {
    int id;                     // Node ID
    int socket_fd;              // Socket file descriptor
    struct sockaddr_in addr;    // Socket address
    pthread_t recv_thread;      // Thread for receiving messages
    bool is_running;            // Flag to control thread execution
    char ip[MAX_IP_STR_LEN];    // Local IP address of this node
    char public_ip[MAX_IP_STR_LEN]; // Public IP address (if behind NAT)
    int public_port;            // Public port (if behind NAT)
    bool is_behind_nat;         // Whether this node is behind NAT
    bool use_upnp;              // Whether to use UPnP for port forwarding
    bool use_discovery;         // Whether to use automatic peer discovery
    bool use_discovery_server;  // Whether to use discovery server
    bool firewall_bypass;       // Whether to use firewall bypass techniques
    NodeInfo peers[MAX_NODES];  // Information about peer nodes
    int peer_count;             // Number of connected peers
    pthread_mutex_t peers_mutex; // Mutex for thread-safe peer list access
    void* dht_data;             // DHT related data (opaque pointer)
    void* rendezvous_data;      // Rendezvous related data (opaque pointer)
    void* turn_data;            // TURN related data (opaque pointer)
    void* ice_data;             // ICE related data (opaque pointer)
} Node;

typedef struct {
    int from_id;                // Sender node ID
    int to_id;                  // Recipient node ID
    char data[MAX_BUFFER];      // Message data
} Message;

// Message types for node protocol
#define MSG_TYPE_DATA 0
#define MSG_TYPE_PING 1
#define MSG_TYPE_PONG 2
#define MSG_TYPE_PEER_LIST 3
#define MSG_TYPE_NAT_TRAVERSAL 4

typedef struct {
    uint8_t type;               // Message type
    uint32_t seq;               // Sequence number
    int from_id;                // Sender node ID
    int to_id;                  // Recipient node ID
    uint16_t data_len;          // Length of data
    char data[MAX_BUFFER];      // Message data
} ProtocolMessage;

// Function prototypes
Node* create_node(int id, const char* ip, int port);
void destroy_node(Node* node);
int add_peer(Node* node, int peer_id, const char* peer_ip, int peer_port);
int add_peer_info(Node* node, NodeInfo* peer_info);
int remove_peer(Node* node, int peer_id);
int connect_to_node(Node* from_node, int to_id);
int send_message(Node* from_node, int to_id, const char* data);
int send_protocol_message(Node* from_node, int to_id, uint8_t type, const char* data, uint16_t data_len);
void* receive_messages(void* arg);
void print_message(const Message* msg);
char* get_local_ip();
int node_enable_nat_traversal(Node* node, const char* stun_server);
int node_enable_upnp(Node* node);
int node_enable_discovery(Node* node);
void node_maintain_peers(Node* node);
void node_share_peer_list(Node* node, int to_id);
int node_punch_hole(Node* from_node, NodeInfo* peer);

#endif /* NODE_H */