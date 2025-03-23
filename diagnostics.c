#include "diagnostics.h"

// Print node status
void print_node_status(Node* node) {
    printf("\n=== Node %d Status ===\n", node->id);
    printf("Local IP: %s\n", node->ip);
    printf("Local Port: %d\n", ntohs(node->addr.sin_port));
    
    if (node->is_behind_nat) {
        printf("Public IP: %s\n", node->public_ip);
        printf("Public Port: %d\n", node->public_port);
        printf("NAT Status: Behind NAT\n");
    } else {
        printf("NAT Status: Direct connection\n");
    }
    
    printf("UPnP: %s\n", node->use_upnp ? "Enabled" : "Disabled");
    printf("Discovery: %s\n", node->use_discovery ? "Enabled" : "Disabled");
    printf("Firewall Bypass: %s\n", node->firewall_bypass ? "Enabled" : "Disabled");
    printf("Connected Peers: %d\n", node->peer_count);
}

// Print peer status
void print_peer_status(Node* node) {
    printf("\n=== Peer Status for Node %d ===\n", node->id);
    
    pthread_mutex_lock(&node->peers_mutex);
    
    if (node->peer_count == 0) {
        printf("No peers connected.\n");
    } else {
        printf("ID\tIP\t\t\tPort\tLast Seen\tPublic\n");
        printf("----------------------------------------------------------\n");
        
        time_t now = time(NULL);
        for (int i = 0; i < node->peer_count; i++) {
            char time_str[64];
            int seconds_ago = (int)(now - node->peers[i].last_seen);
            
            if (seconds_ago < 60) {
                snprintf(time_str, sizeof(time_str), "%d sec ago", seconds_ago);
            } else if (seconds_ago < 3600) {
                snprintf(time_str, sizeof(time_str), "%d min ago", seconds_ago / 60);
            } else {
                snprintf(time_str, sizeof(time_str), "%d hr ago", seconds_ago / 3600);
            }
            
            printf("%d\t%-15s\t%d\t%s\t%s\n", 
                   node->peers[i].id, 
                   node->peers[i].ip, 
                   node->peers[i].port,
                   time_str,
                   node->peers[i].is_public ? "Yes" : "No");
        }
    }
    
    pthread_mutex_unlock(&node->peers_mutex);
}

// Ping a peer and wait for response
int ping_peer(Node* node, int peer_id, int timeout_sec) {
    printf("Pinging node %d...\n", peer_id);
    
    // Send ping message
    char ping_data[64];
    snprintf(ping_data, sizeof(ping_data), "ping:%ld", time(NULL));
    
    if (send_protocol_message(node, peer_id, MSG_TYPE_PING, ping_data, strlen(ping_data)) < 0) {
        printf("Failed to send ping to node %d\n", peer_id);
        return -1;
    }
    
    // Wait for response (in a real implementation, this would be asynchronous)
    printf("Waiting for response from node %d...\n", peer_id);
    sleep(timeout_sec);
    
    // Check if we received a response (this is simplified)
    pthread_mutex_lock(&node->peers_mutex);
    
    int peer_index = -1;
    for (int i = 0; i < node->peer_count; i++) {
        if (node->peers[i].id == peer_id) {
            peer_index = i;
            break;
        }
    }
    
    if (peer_index == -1) {
        pthread_mutex_unlock(&node->peers_mutex);
        printf("Node %d not found in peer list\n", peer_id);
        return -1;
    }
    
    // Check if the peer was seen recently
    time_t now = time(NULL);
    int result = (now - node->peers[peer_index].last_seen <= timeout_sec) ? 0 : -1;
    
    pthread_mutex_unlock(&node->peers_mutex);
    
    if (result == 0) {
        printf("Received response from node %d\n", peer_id);
    } else {
        printf("No response from node %d\n", peer_id);
    }
    
    return result;
}

// Run network diagnostics
void run_network_diagnostics(Node* node) {
    printf("\n=== Running Network Diagnostics for Node %d ===\n", node->id);
    
    // Print node status
    print_node_status(node);
    
    // Print peer status
    print_peer_status(node);
    
    // Check connectivity to each peer
    pthread_mutex_lock(&node->peers_mutex);
    
    if (node->peer_count > 0) {
        printf("\n=== Connectivity Tests ===\n");
        
        for (int i = 0; i < node->peer_count; i++) {
            int peer_id = node->peers[i].id;
            pthread_mutex_unlock(&node->peers_mutex);
            
            ping_peer(node, peer_id, PING_TIMEOUT);
            
            pthread_mutex_lock(&node->peers_mutex);
        }
    }
    
    pthread_mutex_unlock(&node->peers_mutex);
    
    printf("\n=== Diagnostics Complete ===\n");
}

// Log network event
void log_network_event(Node* node, const char* event, const char* details) {
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    printf("[%s] Node %d: %s - %s\n", time_str, node->id, event, details);
    
    // In a real implementation, this would write to a log file
}