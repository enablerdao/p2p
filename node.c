#include "node.h"
#include <errno.h>

// Create a new node
Node* create_node(int id, const char* ip, int port) {
    // Allocate memory for node
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) {
        perror("Failed to allocate memory for node");
        return NULL;
    }
    
    // Initialize node
    memset(node, 0, sizeof(Node));
    node->id = id;
    node->is_running = true;
    node->peer_count = 0;
    
    // Copy IP address
    if (ip) {
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
            // Try to bind to common ports
            perror("Failed to bind to default port, trying firewall-friendly ports");
            
            // Try some common ports
            int common_ports[] = {80, 443, 8080, 8443, 53, 123};
            int num_ports = sizeof(common_ports) / sizeof(common_ports[0]);
            bool bound = false;
            
            for (int i = 0; i < num_ports; i++) {
                memset(&node->addr, 0, sizeof(node->addr));
                node->addr.sin_family = AF_INET;
                node->addr.sin_addr.s_addr = INADDR_ANY;
                node->addr.sin_port = htons(common_ports[i]);
                
                if (bind(node->socket_fd, (struct sockaddr*)&node->addr, sizeof(node->addr)) == 0) {
                    printf("Successfully bound to firewall-friendly port %d\n", common_ports[i]);
                    bound = true;
                    break;
                }
            }
            
            if (!bound) {
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

    int actual_port = ntohs(node->addr.sin_port);
    printf("\n\033[1;38;5;51m╔══════════════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1;38;5;51m║\033[0m \033[1;38;5;226m⚡ NODE CREATED\033[0m                                        \033[1;38;5;51m║\033[0m\n");
    printf("\033[1;38;5;51m╠══════════════════════════════════════════════════════════╣\033[0m\n");
    printf("\033[1;38;5;51m║\033[0m \033[1;38;5;226mID\033[0m:       \033[1;38;5;46m%-44d\033[0m \033[1;38;5;51m║\033[0m\n", id);
    printf("\033[1;38;5;51m║\033[0m \033[1;38;5;226mAddress\033[0m:  \033[1;38;5;46m%-44s\033[0m \033[1;38;5;51m║\033[0m\n", node->ip);
    printf("\033[1;38;5;51m║\033[0m \033[1;38;5;226mPort\033[0m:     \033[1;38;5;46m%-44d\033[0m \033[1;38;5;51m║\033[0m\n", actual_port);
    printf("\033[1;38;5;51m╠══════════════════════════════════════════════════════════╣\033[0m\n");
    printf("\033[1;38;5;51m║\033[0m \033[38;5;252mTo connect to this node from another computer, use:\033[0m      \033[1;38;5;51m║\033[0m\n");
    printf("\033[1;38;5;51m║\033[0m \033[38;5;252m  ./node_network -p %d:%s:%d\033[0m%*s\033[1;38;5;51m║\033[0m\n", 
           id, node->ip, actual_port, 
           (int)(44 - strlen(node->ip) - 15 - (id > 999 ? 4 : (id > 99 ? 3 : (id > 9 ? 2 : 1))) - (actual_port > 9999 ? 5 : (actual_port > 999 ? 4 : (actual_port > 99 ? 3 : (actual_port > 9 ? 2 : 1))))), "");
    printf("\033[1;38;5;51m╚══════════════════════════════════════════════════════════╝\033[0m\n");
    return node;
}

// Clean up and destroy a node
void destroy_node(Node* node) {
    if (!node) {
        return;
    }
    
    // Stop the receive thread
    node->is_running = false;
    pthread_join(node->recv_thread, NULL);
    
    // Close socket
    if (node->socket_fd >= 0) {
        close(node->socket_fd);
    }
    
    // Free DHT and Rendezvous data if present
    if (node->dht_data) {
        free(node->dht_data);
        node->dht_data = NULL;
    }
    
    if (node->rendezvous_data) {
        free(node->rendezvous_data);
        node->rendezvous_data = NULL;
    }
    
    if (node->turn_data) {
        free(node->turn_data);
        node->turn_data = NULL;
    }
    
    if (node->ice_data) {
        free(node->ice_data);
        node->ice_data = NULL;
    }
    
    // Free memory
    free(node);
    
    printf("Node destroyed\n");
}

// Add a peer to a node's peer list
int add_peer(Node* node, int peer_id, const char* peer_ip, int peer_port) {
    // Check if peer already exists
    for (int i = 0; i < node->peer_count; i++) {
        if (node->peers[i].id == peer_id) {
            // Update peer information
            strncpy(node->peers[i].ip, peer_ip, MAX_IP_STR_LEN - 1);
            node->peers[i].ip[MAX_IP_STR_LEN - 1] = '\0';
            node->peers[i].port = peer_port;
            node->peers[i].last_seen = time(NULL);
            printf("Updated peer: Node %d at %s:%d\n", peer_id, peer_ip, peer_port);
            return 0;
        }
    }
    
    // Check if peer list is full
    if (node->peer_count >= MAX_NODES) {
        fprintf(stderr, "Peer list is full\n");
        return -1;
    }
    
    // Add new peer
    node->peers[node->peer_count].id = peer_id;
    strncpy(node->peers[node->peer_count].ip, peer_ip, MAX_IP_STR_LEN - 1);
    node->peers[node->peer_count].ip[MAX_IP_STR_LEN - 1] = '\0';
    node->peers[node->peer_count].port = peer_port;
    node->peers[node->peer_count].last_seen = time(NULL);
    node->peer_count++;
    
    printf("Added peer: Node %d at %s:%d\n", peer_id, peer_ip, peer_port);
    return 0;
}

// Add a peer using NodeInfo structure
int add_peer_info(Node* node, NodeInfo* peer_info) {
    if (!node || !peer_info) {
        return -1;
    }
    
    // Check if peer already exists
    for (int i = 0; i < node->peer_count; i++) {
        if (node->peers[i].id == peer_info->id) {
            // Update peer information
            memcpy(&node->peers[i], peer_info, sizeof(NodeInfo));
            node->peers[i].last_seen = time(NULL);
            printf("Updated peer: Node %d at %s:%d\n", peer_info->id, peer_info->ip, peer_info->port);
            return 0;
        }
    }
    
    // Check if peer list is full
    if (node->peer_count >= MAX_NODES) {
        fprintf(stderr, "Peer list is full\n");
        return -1;
    }
    
    // Add new peer
    memcpy(&node->peers[node->peer_count], peer_info, sizeof(NodeInfo));
    node->peers[node->peer_count].last_seen = time(NULL);
    node->peer_count++;
    
    printf("Added peer: Node %d at %s:%d\n", peer_info->id, peer_info->ip, peer_info->port);
    return 0;
}

// Remove a peer from a node's peer list
int remove_peer(Node* node, int peer_id) {
    // Find peer in the list
    int peer_index = -1;
    for (int i = 0; i < node->peer_count; i++) {
        if (node->peers[i].id == peer_id) {
            peer_index = i;
            break;
        }
    }
    
    if (peer_index == -1) {
        fprintf(stderr, "Peer node %d not found\n", peer_id);
        return -1;
    }
    
    // Remove peer by shifting all peers after it
    for (int i = peer_index; i < node->peer_count - 1; i++) {
        node->peers[i] = node->peers[i + 1];
    }
    
    node->peer_count--;
    printf("Removed peer: Node %d\n", peer_id);
    return 0;
}

// Connect to another node
int connect_to_node(Node* from_node, int to_id) {
    // Find the peer in the peer list
    int peer_index = -1;
    for (int i = 0; i < from_node->peer_count; i++) {
        if (from_node->peers[i].id == to_id) {
            peer_index = i;
            break;
        }
    }
    
    if (peer_index == -1) {
        fprintf(stderr, "Peer node %d not found\n", to_id);
        return -1;
    }
    
    // Send a test message
    char message[MAX_BUFFER];
    snprintf(message, MAX_BUFFER, "Hello from Node %d! This is a connection test.", from_node->id);
    
    return send_message(from_node, to_id, message);
}

// Send a protocol message to another node
int send_protocol_message(Node* from_node, int to_id, uint8_t type, const char* data, uint16_t data_len) {
    // Find the peer in the peer list
    int peer_index = -1;
    for (int i = 0; i < from_node->peer_count; i++) {
        if (from_node->peers[i].id == to_id) {
            peer_index = i;
            break;
        }
    }
    
    if (peer_index == -1) {
        fprintf(stderr, "Peer node %d not found\n", to_id);
        return -1;
    }
    
    // Create protocol message
    ProtocolMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    msg.seq = 0; // TODO: Implement sequence numbers
    msg.from_id = from_node->id;
    msg.to_id = to_id;
    msg.data_len = data_len;
    
    if (data_len > 0 && data != NULL) {
        memcpy(msg.data, data, data_len < MAX_BUFFER ? data_len : MAX_BUFFER - 1);
        msg.data[data_len < MAX_BUFFER ? data_len : MAX_BUFFER - 1] = '\0';
    }
    
    // Set up destination address
    struct sockaddr_in to_addr;
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_addr.s_addr = inet_addr(from_node->peers[peer_index].ip);
    to_addr.sin_port = htons(from_node->peers[peer_index].port);
    
    // Send message
    if (sendto(from_node->socket_fd, &msg, sizeof(msg), 0, 
               (struct sockaddr*)&to_addr, sizeof(to_addr)) < 0) {
        perror("Failed to send protocol message");
        return -1;
    }
    
    // Log to console
    printf("Node %d sent protocol message type %d to Node %d at %s:%d\n", 
           from_node->id, type, to_id, from_node->peers[peer_index].ip, 
           from_node->peers[peer_index].port);
    
    return 0;
}

// Send a message to another node
int send_message(Node* from_node, int to_id, const char* data) {
    // Find the peer in the peer list
    int peer_index = -1;
    for (int i = 0; i < from_node->peer_count; i++) {
        if (from_node->peers[i].id == to_id) {
            peer_index = i;
            break;
        }
    }
    
    if (peer_index == -1) {
        fprintf(stderr, "Peer node %d not found\n", to_id);
        return -1;
    }
    
    // Create message
    Message msg;
    msg.from_id = from_node->id;
    msg.to_id = to_id;
    strncpy(msg.data, data, MAX_BUFFER - 1);
    msg.data[MAX_BUFFER - 1] = '\0';
    
    // Set up destination address
    struct sockaddr_in to_addr;
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_addr.s_addr = inet_addr(from_node->peers[peer_index].ip);
    to_addr.sin_port = htons(from_node->peers[peer_index].port);
    
    // Send message
    if (sendto(from_node->socket_fd, &msg, sizeof(msg), 0, 
               (struct sockaddr*)&to_addr, sizeof(to_addr)) < 0) {
        perror("Failed to send message");
        return -1;
    }
    
    // Print a more visible message notification
    printf("\n\033[1;38;5;117m"); // Bold bright cyan text
    printf("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
    printf("┃ \033[1;38;5;226m✉️  MESSAGE SENT\033[1;38;5;117m                                     ┃\n");
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ \033[1;38;5;226mFrom\033[1;38;5;117m:    Node \033[1;38;5;46m%-42d\033[1;38;5;117m ┃\n", from_node->id);
    printf("┃ \033[1;38;5;226mTo\033[1;38;5;117m:      Node \033[1;38;5;46m%d\033[1;38;5;117m at \033[1;38;5;46m%s:%d\033[1;38;5;117m%*s ┃\n", 
           to_id, from_node->peers[peer_index].ip, from_node->peers[peer_index].port,
           (int)(38 - strlen(from_node->peers[peer_index].ip) - (to_id > 999 ? 4 : (to_id > 99 ? 3 : (to_id > 9 ? 2 : 1))) - (from_node->peers[peer_index].port > 9999 ? 5 : (from_node->peers[peer_index].port > 999 ? 4 : (from_node->peers[peer_index].port > 99 ? 3 : (from_node->peers[peer_index].port > 9 ? 2 : 1))))), "");
    printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
    printf("┃ \033[1;38;5;226mContent\033[1;38;5;117m:                                             ┃\n");
    printf("┃ \033[1;38;5;255m%.*s\033[1;38;5;117m%*s ┃\n", 
           50, data, 
           (int)(50 - (strlen(data) > 50 ? 50 : strlen(data))), "");
    if (strlen(data) > 50) {
        printf("┃ \033[1;38;5;255m%.*s\033[1;38;5;117m%*s ┃\n", 
               (int)(strlen(data) - 50 > 50 ? 50 : strlen(data) - 50), 
               data + 50,
               (int)(50 - (strlen(data) - 50 > 50 ? 50 : strlen(data) - 50)), "");
    }
    printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n");
    printf("\033[0m"); // Reset text formatting
    
    // Also log to console in standard format
    printf("Node %d sent message to Node %d at %s:%d: %s\n", 
           from_node->id, to_id, from_node->peers[peer_index].ip, 
           from_node->peers[peer_index].port, data);
    
    return 0;
}

// Thread function to receive messages
void* receive_messages(void* arg) {
    Node* node = (Node*)arg;
    
    while (node->is_running) {
        // Set up buffer for incoming message
        Message msg;
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        
        // Receive message
        int bytes = recvfrom(node->socket_fd, &msg, sizeof(msg), 0, 
                            (struct sockaddr*)&sender_addr, &sender_len);
        
        if (bytes < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Error receiving message");
            }
            continue;
        }

        // Check if message is for this node
        if (msg.to_id == node->id) {
            char sender_ip[MAX_IP_STR_LEN];
            inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, MAX_IP_STR_LEN);
            
            // Print a more visible message notification
            printf("\n\033[1;38;5;83m"); // Bold bright green text
            printf("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
            printf("┃ \033[1;38;5;226m📩 MESSAGE RECEIVED\033[1;38;5;83m                                  ┃\n");
            printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
            printf("┃ \033[1;38;5;226mTo\033[1;38;5;83m:      Node \033[1;38;5;46m%-42d\033[1;38;5;83m ┃\n", node->id);
            printf("┃ \033[1;38;5;226mFrom\033[1;38;5;83m:    Node \033[1;38;5;46m%d\033[1;38;5;83m at \033[1;38;5;46m%s:%d\033[1;38;5;83m%*s ┃\n", 
                   msg.from_id, sender_ip, ntohs(sender_addr.sin_port),
                   (int)(38 - strlen(sender_ip) - (msg.from_id > 999 ? 4 : (msg.from_id > 99 ? 3 : (msg.from_id > 9 ? 2 : 1))) - (ntohs(sender_addr.sin_port) > 9999 ? 5 : (ntohs(sender_addr.sin_port) > 999 ? 4 : (ntohs(sender_addr.sin_port) > 99 ? 3 : (ntohs(sender_addr.sin_port) > 9 ? 2 : 1))))), "");
            printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");
            printf("┃ \033[1;38;5;226mContent\033[1;38;5;83m:                                             ┃\n");
            printf("┃ \033[1;38;5;255m%.*s\033[1;38;5;83m%*s ┃\n", 
                   50, msg.data, 
                   (int)(50 - (strlen(msg.data) > 50 ? 50 : strlen(msg.data))), "");
            if (strlen(msg.data) > 50) {
                printf("┃ \033[1;38;5;255m%.*s\033[1;38;5;83m%*s ┃\n", 
                       (int)(strlen(msg.data) - 50 > 50 ? 50 : strlen(msg.data) - 50), 
                       msg.data + 50,
                       (int)(50 - (strlen(msg.data) - 50 > 50 ? 50 : strlen(msg.data) - 50)), "");
            }
            printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n");
            printf("\033[0m"); // Reset text formatting
            
            // Also log to console in standard format
            printf("Node %d received message from Node %d at %s:%d: %s\n", 
                   node->id, msg.from_id, sender_ip, ntohs(sender_addr.sin_port), msg.data);
            
            // Play a sound alert (ASCII bell)
            printf("\a");
            fflush(stdout);
        }
    }
    
    return NULL;
}

// Print a message
void print_message(const Message* msg) {
    printf("Message from Node %d to Node %d: %s\n", msg->from_id, msg->to_id, msg->data);
}

// Get local IP address
char* get_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    static char ip[MAX_IP_STR_LEN];
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return NULL;
    }
    
    // Look for non-loopback IPv4 addresses
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            
            // Skip loopback addresses
            if (ntohl(addr->sin_addr.s_addr) == INADDR_LOOPBACK) {
                continue;
            }
            
            // Convert IP to string
            inet_ntop(AF_INET, &addr->sin_addr, ip, MAX_IP_STR_LEN);
            
            // Skip link-local addresses (169.254.x.x)
            if (strncmp(ip, "169.254.", 8) == 0) {
                continue;
            }
            
            // Found a suitable address
            break;
        }
    }
    
    freeifaddrs(ifaddr);
    
    // If no suitable address was found, use loopback
    if (ifa == NULL) {
        strncpy(ip, "127.0.0.1", MAX_IP_STR_LEN);
    }
    
    return ip;
}

// These functions are defined in nat_traversal.c and firewall.c