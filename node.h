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

#define MAX_NODES 10
#define MAX_BUFFER 1024
#define BASE_PORT 8000
#define MAX_IP_STR_LEN 16  // "xxx.xxx.xxx.xxx\0"

// Structure to hold node connection information
typedef struct {
    int id;                     // Node ID
    char ip[MAX_IP_STR_LEN];    // IP address as string
    int port;                   // Port number
} NodeInfo;

typedef struct {
    int id;                     // Node ID
    int socket_fd;              // Socket file descriptor
    struct sockaddr_in addr;    // Socket address
    pthread_t recv_thread;      // Thread for receiving messages
    bool is_running;            // Flag to control thread execution
    char ip[MAX_IP_STR_LEN];    // IP address of this node
    NodeInfo peers[MAX_NODES];  // Information about peer nodes
    int peer_count;             // Number of connected peers
} Node;

typedef struct {
    int from_id;                // Sender node ID
    int to_id;                  // Recipient node ID
    char data[MAX_BUFFER];      // Message data
} Message;

// Function prototypes
Node* create_node(int id, const char* ip, int port);
void destroy_node(Node* node);
int add_peer(Node* node, int peer_id, const char* peer_ip, int peer_port);
int connect_to_node(Node* from_node, int to_id);
int send_message(Node* from_node, int to_id, const char* data);
void* receive_messages(void* arg);
void print_message(const Message* msg);
char* get_local_ip();

#endif /* NODE_H */