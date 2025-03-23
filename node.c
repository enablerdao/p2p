#include "node.h"

// Get the local IP address
char* get_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    static char ip[MAX_IP_STR_LEN];
    int family, s;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return NULL;
    }
    
    // Look for a non-loopback IPv4 address
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
            
        family = ifa->ifa_addr->sa_family;
        
        if (family == AF_INET) {
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                           ip, MAX_IP_STR_LEN, NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                freeifaddrs(ifaddr);
                return NULL;
            }
            
            // Skip loopback addresses
            if (strcmp(ip, "127.0.0.1") != 0) {
                freeifaddrs(ifaddr);
                return ip;
            }
        }
    }
    
    // If no suitable address found, use loopback
    strcpy(ip, "127.0.0.1");
    freeifaddrs(ifaddr);
    return ip;
}

// Create a new node with the given ID, IP and port
Node* create_node(int id, const char* ip, int port) {
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) {
        perror("Failed to allocate memory for node");
        return NULL;
    }

    // Initialize node
    node->id = id;
    node->is_running = true;
    node->peer_count = 0;
    
    // Set IP address
    if (ip == NULL || strlen(ip) == 0) {
        char* local_ip = get_local_ip();
        if (local_ip) {
            strncpy(node->ip, local_ip, MAX_IP_STR_LEN - 1);
            node->ip[MAX_IP_STR_LEN - 1] = '\0';
        } else {
            strcpy(node->ip, "127.0.0.1");
        }
    } else {
        strncpy(node->ip, ip, MAX_IP_STR_LEN - 1);
        node->ip[MAX_IP_STR_LEN - 1] = '\0';
    }

    // Create socket
    node->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (node->socket_fd < 0) {
        perror("Failed to create socket");
        free(node);
        return NULL;
    }
    
    // Enable socket reuse
    int reuse = 1;
    if (setsockopt(node->socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        // Not fatal, continue
    }
    
    // Set up address
    memset(&node->addr, 0, sizeof(node->addr));
    node->addr.sin_family = AF_INET;
    node->addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
    node->addr.sin_port = htons(port == 0 ? BASE_PORT + id : port);

    // Bind socket
    if (bind(node->socket_fd, (struct sockaddr*)&node->addr, sizeof(node->addr)) < 0) {
        // If binding fails, try firewall-friendly ports if firewall bypass is enabled
        if (node->firewall_bypass) {
            int new_port = try_firewall_friendly_ports(node, port == 0 ? BASE_PORT + id : port);
            if (new_port < 0) {
                perror("Failed to bind socket to any port");
                close(node->socket_fd);
                free(node);
                return NULL;
            }
        } else {
            perror("Failed to bind socket");
            close(node->socket_fd);
            free(node);
            return NULL;
        }
    }

    // Start receive thread
    if (pthread_create(&node->recv_thread, NULL, receive_messages, node) != 0) {
        perror("Failed to create receive thread");
        close(node->socket_fd);
        free(node);
        return NULL;
    }

    printf("Node %d created and listening on %s:%d\n", id, node->ip, 
           port == 0 ? BASE_PORT + id : port);
    return node;
}

// Clean up and destroy a node
void destroy_node(Node* node) {
    if (!node) return;

    // Stop the receive thread
    node->is_running = false;
    pthread_join(node->recv_thread, NULL);

    // Close socket and free memory
    close(node->socket_fd);
    free(node);
}

// Add a peer node to the node's peer list
int add_peer(Node* node, int peer_id, const char* peer_ip, int peer_port) {
    if (node->peer_count >= MAX_NODES) {
        fprintf(stderr, "Peer list is full\n");
        return -1;
    }
    
    // Add peer to the list
    node->peers[node->peer_count].id = peer_id;
    strncpy(node->peers[node->peer_count].ip, peer_ip, MAX_IP_STR_LEN - 1);
    node->peers[node->peer_count].ip[MAX_IP_STR_LEN - 1] = '\0';
    node->peers[node->peer_count].port = peer_port == 0 ? BASE_PORT + peer_id : peer_port;
    
    node->peer_count++;
    
    printf("Node %d added peer: Node %d at %s:%d\n", 
           node->id, peer_id, peer_ip, node->peers[node->peer_count-1].port);
    
    return 0;
}

// Connect to another node by ID
int connect_to_node(Node* from_node, int to_id) {
    // Find the peer in the peer list
    for (int i = 0; i < from_node->peer_count; i++) {
        if (from_node->peers[i].id == to_id) {
            printf("Node %d connected to Node %d at %s:%d\n", 
                   from_node->id, to_id, from_node->peers[i].ip, from_node->peers[i].port);
            return 0;
        }
    }
    
    fprintf(stderr, "Node %d not found in peer list\n", to_id);
    return -1;
}

// Send a message from one node to another
int send_message(Node* from_node, int to_id, const char* data) {
    Message msg;
    struct sockaddr_in to_addr;
    
    // Find the peer in the peer list
    int peer_index = -1;
    for (int i = 0; i < from_node->peer_count; i++) {
        if (from_node->peers[i].id == to_id) {
            peer_index = i;
            break;
        }
    }
    
    if (peer_index == -1) {
        fprintf(stderr, "Node %d not found in peer list\n", to_id);
        return -1;
    }

    // Prepare message
    msg.from_id = from_node->id;
    msg.to_id = to_id;
    strncpy(msg.data, data, MAX_BUFFER - 1);
    msg.data[MAX_BUFFER - 1] = '\0';

    // Set up destination address
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_addr.s_addr = inet_addr(from_node->peers[peer_index].ip);
    to_addr.sin_port = htons(from_node->peers[peer_index].port);

    // Send message
    ssize_t sent = sendto(from_node->socket_fd, &msg, sizeof(msg), 0,
                         (struct sockaddr*)&to_addr, sizeof(to_addr));
    
    if (sent < 0) {
        perror("Failed to send message");
        return -1;
    }

    printf("Node %d sent message to Node %d at %s:%d: %s\n", 
           from_node->id, to_id, from_node->peers[peer_index].ip, 
           from_node->peers[peer_index].port, data);
    return 0;
}

// Thread function to receive messages
void* receive_messages(void* arg) {
    Node* node = (Node*)arg;
    Message msg;
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    while (node->is_running) {
        // Receive message
        ssize_t received = recvfrom(node->socket_fd, &msg, sizeof(msg), 0,
                                   (struct sockaddr*)&sender_addr, &addr_len);
        
        if (received < 0) {
            perror("Error receiving message");
            continue;
        }

        // Check if message is for this node
        if (msg.to_id == node->id) {
            char sender_ip[MAX_IP_STR_LEN];
            inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, MAX_IP_STR_LEN);
            
            printf("Node %d received message from Node %d at %s:%d: %s\n", 
                   node->id, msg.from_id, sender_ip, ntohs(sender_addr.sin_port), msg.data);
        }
    }

    return NULL;
}

// Print a received message
void print_message(const Message* msg) {
    printf("Node %d received message from Node %d: %s\n", 
           msg->to_id, msg->from_id, msg->data);
}