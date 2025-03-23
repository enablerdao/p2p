#include "discovery.h"

static int discovery_socket = -1;
static pthread_t discovery_thread_id;
static bool discovery_running = false;

// Initialize discovery service
int discovery_init(Node* node) {
    // Create socket for discovery
    discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket < 0) {
        perror("Failed to create discovery socket");
        return -1;
    }
    
    // Enable broadcast
    int broadcast = 1;
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        perror("Failed to set SO_BROADCAST");
        close(discovery_socket);
        discovery_socket = -1;
        return -1;
    }
    
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = DISCOVERY_TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set socket timeout");
        close(discovery_socket);
        discovery_socket = -1;
        return -1;
    }
    
    // Bind to discovery port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(DISCOVERY_PORT);
    
    if (bind(discovery_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to bind discovery socket");
        close(discovery_socket);
        discovery_socket = -1;
        return -1;
    }
    
    // Join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(DISCOVERY_MULTICAST_ADDR);
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(discovery_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("Failed to join multicast group");
        close(discovery_socket);
        discovery_socket = -1;
        return -1;
    }
    
    // Start discovery thread
    discovery_running = true;
    if (pthread_create(&discovery_thread_id, NULL, discovery_thread, node) != 0) {
        perror("Failed to create discovery thread");
        close(discovery_socket);
        discovery_socket = -1;
        discovery_running = false;
        return -1;
    }
    
    printf("Node discovery service started\n");
    return 0;
}

// Clean up discovery service
void discovery_cleanup() {
    if (discovery_running) {
        discovery_running = false;
        pthread_join(discovery_thread_id, NULL);
    }
    
    if (discovery_socket >= 0) {
        close(discovery_socket);
        discovery_socket = -1;
    }
}

// Announce node presence
void discovery_announce(Node* node) {
    if (discovery_socket < 0) {
        fprintf(stderr, "Discovery service not initialized\n");
        return;
    }
    
    // Create announcement message
    char message[256];
    snprintf(message, sizeof(message), "NODE_ANNOUNCE:%d:%s:%d",
             node->id, node->ip, ntohs(node->addr.sin_port));
    
    // Set up multicast address
    struct sockaddr_in mcast_addr;
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_addr.s_addr = inet_addr(DISCOVERY_MULTICAST_ADDR);
    mcast_addr.sin_port = htons(DISCOVERY_PORT);
    
    // Send announcement
    if (sendto(discovery_socket, message, strlen(message), 0,
               (struct sockaddr*)&mcast_addr, sizeof(mcast_addr)) < 0) {
        perror("Failed to send node announcement");
    } else {
        printf("Node %d announced presence\n", node->id);
    }
}

// Listen for node announcements
void discovery_listen(Node* node) {
    if (discovery_socket < 0) {
        fprintf(stderr, "Discovery service not initialized\n");
        return;
    }
    
    // Receive announcements
    char buffer[256];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(discovery_socket, buffer, sizeof(buffer) - 1, 0,
                               (struct sockaddr*)&from_addr, &from_len);
    
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Failed to receive node announcement");
        }
        return;
    }
    
    buffer[received] = '\0';
    
    // Parse announcement
    if (strncmp(buffer, "NODE_ANNOUNCE:", 14) == 0) {
        int peer_id;
        char peer_ip[MAX_IP_STR_LEN];
        int peer_port;
        
        if (sscanf(buffer, "NODE_ANNOUNCE:%d:%15[^:]:%d", &peer_id, peer_ip, &peer_port) == 3) {
            // Skip our own announcements
            if (peer_id == node->id) {
                return;
            }
            
            // Check if we already know this peer
            bool known_peer = false;
            for (int i = 0; i < node->peer_count; i++) {
                if (node->peers[i].id == peer_id) {
                    known_peer = true;
                    break;
                }
            }
            
            // Add new peer
            if (!known_peer) {
                add_peer(node, peer_id, peer_ip, peer_port);
                connect_to_node(node, peer_id);
                
                // Send a welcome message
                char welcome[MAX_BUFFER];
                snprintf(welcome, MAX_BUFFER, "Hello from node %d! I discovered you via multicast.", node->id);
                send_message(node, peer_id, welcome);
            }
        }
    }
}

// Discovery thread function
void* discovery_thread(void* arg) {
    Node* node = (Node*)arg;
    
    while (discovery_running) {
        // Announce our presence
        discovery_announce(node);
        
        // Listen for announcements from other nodes
        for (int i = 0; i < 10; i++) { // Check for announcements multiple times between our own announcements
            if (!discovery_running) break;
            discovery_listen(node);
            sleep(1);
        }
    }
    
    return NULL;
}