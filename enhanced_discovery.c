#include "enhanced_discovery.h"
#include "firewall.h"
#include <time.h>
#include <errno.h>

static int discovery_socket = -1;
static struct sockaddr_in multicast_addr;
static pthread_t discovery_thread_id;
static bool discovery_running = false;
static uint32_t sequence_counter = 0;

// Initialize enhanced discovery
int enhanced_discovery_init(Node* node) {
    // Create UDP socket
    discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket < 0) {
        perror("Failed to create discovery socket");
        return -1;
    }
    
    // Enable address reuse
    int reuse = 1;
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Failed to set SO_REUSEADDR");
        close(discovery_socket);
        return -1;
    }
    
    // Set TTL for multicast packets
    unsigned char ttl = ENHANCED_DISCOVERY_TTL;
    if (setsockopt(discovery_socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("Failed to set IP_MULTICAST_TTL");
        close(discovery_socket);
        return -1;
    }
    
    // Set up multicast address
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(ENHANCED_MULTICAST_ADDR);
    multicast_addr.sin_port = htons(ENHANCED_DISCOVERY_PORT);
    
    // Bind to discovery port
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(ENHANCED_DISCOVERY_PORT);
    
    if (bind(discovery_socket, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("Failed to bind discovery socket");
        close(discovery_socket);
        return -1;
    }
    
    // Join multicast group on all interfaces
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            
            // Skip loopback interface
            if (ifa->ifa_flags & IFF_LOOPBACK) {
                continue;
            }
            
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = inet_addr(ENHANCED_MULTICAST_ADDR);
            mreq.imr_interface.s_addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
            
            if (setsockopt(discovery_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
                // Not fatal, just log and continue
                printf("Warning: Failed to join multicast group on interface %s: %s\n", 
                       ifa->ifa_name, strerror(errno));
            } else {
                printf("Joined multicast group on interface %s\n", ifa->ifa_name);
            }
        }
        
        freeifaddrs(ifaddr);
    }
    
    // Start discovery thread
    discovery_running = true;
    if (pthread_create(&discovery_thread_id, NULL, enhanced_discovery_thread, node) != 0) {
        perror("Failed to create discovery thread");
        close(discovery_socket);
        discovery_running = false;
        return -1;
    }
    
    printf("Enhanced discovery initialized for node %d\n", node->id);
    
    // Send initial announcement and query
    enhanced_discovery_send_announcement(node);
    enhanced_discovery_send_query(node);
    
    return 0;
}

// Send discovery announcement
int enhanced_discovery_send_announcement(Node* node) {
    EnhancedDiscoveryMessage msg;
    memset(&msg, 0, sizeof(msg));
    
    msg.type = DISC_MSG_ANNOUNCE;
    msg.node_id = node->id;
    strncpy(msg.ip, node->ip, MAX_IP_STR_LEN - 1);
    msg.port = ntohs(node->addr.sin_port);
    
    if (node->is_behind_nat) {
        strncpy(msg.public_ip, node->public_ip, MAX_IP_STR_LEN - 1);
        msg.public_port = node->public_port;
        msg.is_public = false;
    } else {
        strncpy(msg.public_ip, node->ip, MAX_IP_STR_LEN - 1);
        msg.public_port = ntohs(node->addr.sin_port);
        msg.is_public = true;
    }
    
    msg.timestamp = (uint32_t)time(NULL);
    msg.sequence = ++sequence_counter;
    
    if (sendto(discovery_socket, &msg, sizeof(msg), 0, 
               (struct sockaddr*)&multicast_addr, sizeof(multicast_addr)) < 0) {
        perror("Failed to send discovery announcement");
        return -1;
    }
    
    printf("Sent discovery announcement for node %d\n", node->id);
    return 0;
}

// Send discovery query
int enhanced_discovery_send_query(Node* node) {
    EnhancedDiscoveryMessage msg;
    memset(&msg, 0, sizeof(msg));
    
    msg.type = DISC_MSG_QUERY;
    msg.node_id = node->id;
    strncpy(msg.ip, node->ip, MAX_IP_STR_LEN - 1);
    msg.port = ntohs(node->addr.sin_port);
    
    if (node->is_behind_nat) {
        strncpy(msg.public_ip, node->public_ip, MAX_IP_STR_LEN - 1);
        msg.public_port = node->public_port;
        msg.is_public = false;
    } else {
        strncpy(msg.public_ip, node->ip, MAX_IP_STR_LEN - 1);
        msg.public_port = ntohs(node->addr.sin_port);
        msg.is_public = true;
    }
    
    msg.timestamp = (uint32_t)time(NULL);
    msg.sequence = ++sequence_counter;
    
    if (sendto(discovery_socket, &msg, sizeof(msg), 0, 
               (struct sockaddr*)&multicast_addr, sizeof(multicast_addr)) < 0) {
        perror("Failed to send discovery query");
        return -1;
    }
    
    printf("Sent discovery query for node %d\n", node->id);
    return 0;
}

// Process discovery message
int enhanced_discovery_process_message(Node* node, EnhancedDiscoveryMessage* msg, struct sockaddr_in* sender_addr) {
    // Skip our own messages
    if (msg->node_id == node->id) {
        return 0;
    }
    
    // Check if we already know this peer
    bool known_peer = false;
    
    pthread_mutex_lock(&node->peers_mutex);
    
    for (int i = 0; i < node->peer_count; i++) {
        if (node->peers[i].id == msg->node_id) {
            known_peer = true;
            // Update last seen time
            node->peers[i].last_seen = time(NULL);
            break;
        }
    }
    
    pthread_mutex_unlock(&node->peers_mutex);
    
    // If it's a query and we don't know the peer, respond with an announcement
    if (msg->type == DISC_MSG_QUERY && !known_peer) {
        enhanced_discovery_send_announcement(node);
    }
    
    // If we already know this peer, nothing more to do
    if (known_peer) {
        return 0;
    }
    
    // Add new peer
    printf("Discovered new peer via multicast: Node %d at %s:%d\n", 
           msg->node_id, msg->is_public ? msg->ip : msg->public_ip, 
           msg->is_public ? msg->port : msg->public_port);
    
    add_peer(node, msg->node_id, msg->is_public ? msg->ip : msg->public_ip, 
             msg->is_public ? msg->port : msg->public_port);
    
    // If both nodes are behind NAT, try hole punching
    if (node->is_behind_nat && !msg->is_public) {
        pthread_mutex_lock(&node->peers_mutex);
        
        for (int i = 0; i < node->peer_count; i++) {
            if (node->peers[i].id == msg->node_id) {
                if (node->firewall_bypass) {
                    punch_multiple_ports(node, &node->peers[i]);
                } else {
                    node_punch_hole(node, &node->peers[i]);
                }
                break;
            }
        }
        
        pthread_mutex_unlock(&node->peers_mutex);
    }
    
    // Connect to the new peer
    connect_to_node(node, msg->node_id);
    
    return 1;
}

// Discovery thread function
void* enhanced_discovery_thread(void* arg) {
    Node* node = (Node*)arg;
    
    // Set up timeout for recvfrom
    struct timeval tv;
    tv.tv_sec = ENHANCED_DISCOVERY_TIMEOUT;
    tv.tv_usec = 0;
    
    if (setsockopt(discovery_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set SO_RCVTIMEO");
    }
    
    EnhancedDiscoveryMessage msg;
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    
    time_t last_announcement = 0;
    time_t last_query = 0;
    
    while (discovery_running && node->is_running) {
        time_t now = time(NULL);
        
        // Send periodic announcements
        if (now - last_announcement >= ENHANCED_DISCOVERY_INTERVAL) {
            enhanced_discovery_send_announcement(node);
            last_announcement = now;
        }
        
        // Send periodic queries (less frequently)
        if (now - last_query >= ENHANCED_DISCOVERY_INTERVAL * 3) {
            enhanced_discovery_send_query(node);
            last_query = now;
        }
        
        // Receive discovery messages
        memset(&msg, 0, sizeof(msg));
        memset(&sender_addr, 0, sizeof(sender_addr));
        
        int bytes = recvfrom(discovery_socket, &msg, sizeof(msg), 0, 
                            (struct sockaddr*)&sender_addr, &sender_len);
        
        if (bytes < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Error receiving discovery message");
            }
            // Timeout or error, continue
            continue;
        }
        
        if (bytes == sizeof(EnhancedDiscoveryMessage)) {
            enhanced_discovery_process_message(node, &msg, &sender_addr);
        }
    }
    
    return NULL;
}

// Clean up discovery resources
void enhanced_discovery_cleanup() {
    discovery_running = false;
    
    if (discovery_socket >= 0) {
        close(discovery_socket);
        discovery_socket = -1;
    }
    
    if (discovery_running) {
        pthread_join(discovery_thread_id, NULL);
    }
    
    printf("Enhanced discovery cleaned up\n");
}