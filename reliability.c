#include "reliability.h"

static pthread_t reliability_thread_id;
static bool reliability_running = false;

// Send keepalive message to all peers
void send_keepalive(Node* node) {
    pthread_mutex_lock(&node->peers_mutex);
    
    for (int i = 0; i < node->peer_count; i++) {
        // Send a ping message
        send_protocol_message(node, node->peers[i].id, MSG_TYPE_PING, "ping", 4);
    }
    
    pthread_mutex_unlock(&node->peers_mutex);
}

// Attempt to reconnect to a peer
int reconnect_to_peer(Node* node, int peer_id) {
    pthread_mutex_lock(&node->peers_mutex);
    
    // Find the peer
    int peer_index = -1;
    for (int i = 0; i < node->peer_count; i++) {
        if (node->peers[i].id == peer_id) {
            peer_index = i;
            break;
        }
    }
    
    if (peer_index == -1) {
        pthread_mutex_unlock(&node->peers_mutex);
        return -1;
    }
    
    // Try to reconnect
    printf("Attempting to reconnect to node %d at %s:%d\n", 
           peer_id, node->peers[peer_index].ip, node->peers[peer_index].port);
    
    // If using NAT traversal, try hole punching
    if (node->is_behind_nat) {
        if (node->firewall_bypass) {
            punch_multiple_ports(node, &node->peers[peer_index]);
        } else {
            node_punch_hole(node, &node->peers[peer_index]);
        }
    }
    
    // Send a ping message
    send_protocol_message(node, peer_id, MSG_TYPE_PING, "reconnect", 9);
    
    // Update last seen time to avoid immediate removal
    node->peers[peer_index].last_seen = time(NULL);
    
    pthread_mutex_unlock(&node->peers_mutex);
    return 0;
}

// Reliability thread function
void* reliability_thread(void* arg) {
    Node* node = (Node*)arg;
    time_t last_keepalive = 0;
    
    while (reliability_running && node->is_running) {
        time_t now = time(NULL);
        
        // Send keepalive messages periodically
        if (now - last_keepalive >= KEEPALIVE_INTERVAL) {
            send_keepalive(node);
            last_keepalive = now;
        }
        
        // Check for peers that need reconnection
        pthread_mutex_lock(&node->peers_mutex);
        
        for (int i = 0; i < node->peer_count; i++) {
            // If we haven't seen this peer for a while but not long enough to remove
            if (now - node->peers[i].last_seen > KEEPALIVE_INTERVAL * 2 && 
                now - node->peers[i].last_seen < 300) {
                
                // Try to reconnect
                reconnect_to_peer(node, node->peers[i].id);
            }
        }
        
        pthread_mutex_unlock(&node->peers_mutex);
        
        // Sleep for a short time
        sleep(1);
    }
    
    return NULL;
}

// Start reliability service
int start_reliability_service(Node* node) {
    if (reliability_running) {
        return 0;  // Already running
    }
    
    reliability_running = true;
    if (pthread_create(&reliability_thread_id, NULL, reliability_thread, node) != 0) {
        perror("Failed to create reliability thread");
        reliability_running = false;
        return -1;
    }
    
    printf("Reliability service started for node %d\n", node->id);
    return 0;
}

// Stop reliability service
void stop_reliability_service(Node* node) {
    if (!reliability_running) {
        return;
    }
    
    reliability_running = false;
    pthread_join(reliability_thread_id, NULL);
    
    printf("Reliability service stopped for node %d\n", node->id);
}