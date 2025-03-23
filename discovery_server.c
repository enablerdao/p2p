#include "discovery_server.h"
#include "firewall.h"

static pthread_t discovery_thread_id;
static bool discovery_running = false;

// Register this node with the discovery server
int register_with_discovery_server(Node* node, const char* server, int port) {
    // In a real implementation, this would connect to a central server
    // For this demo, we'll simulate it with a local file
    
    char node_info[256];
    
    // Format: id:ip:port:public_ip:public_port:is_public
    if (node->is_behind_nat) {
        snprintf(node_info, sizeof(node_info), "%d:%s:%d:%s:%d:0",
                node->id, node->ip, ntohs(node->addr.sin_port),
                node->public_ip, node->public_port);
    } else {
        snprintf(node_info, sizeof(node_info), "%d:%s:%d:%s:%d:1",
                node->id, node->ip, ntohs(node->addr.sin_port),
                node->ip, ntohs(node->addr.sin_port));
    }
    
    // Write to a local file (simulating a discovery server)
    char filename[64];
    snprintf(filename, sizeof(filename), "/tmp/p2p_discovery_%d.txt", node->id);
    
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Failed to open discovery file");
        return -1;
    }
    
    fprintf(file, "%s\n", node_info);
    fclose(file);
    
    printf("Registered node %d with discovery server (simulated)\n", node->id);
    
    // Also write to a common file that all nodes can read
    file = fopen("/tmp/p2p_discovery_all.txt", "a");
    if (file) {
        fprintf(file, "%s\n", node_info);
        fclose(file);
    }
    
    return 0;
}

// Query the discovery server for other nodes
int query_discovery_server(Node* node, const char* server, int port) {
    // In a real implementation, this would query a central server
    // For this demo, we'll read from the common file
    
    FILE* file = fopen("/tmp/p2p_discovery_all.txt", "r");
    if (!file) {
        // No discovery file yet, not an error
        return 0;
    }
    
    char line[256];
    int count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Parse node info
        int peer_id;
        char peer_ip[MAX_IP_STR_LEN];
        int peer_port;
        char peer_public_ip[MAX_IP_STR_LEN];
        int peer_public_port;
        int is_public;
        
        if (sscanf(line, "%d:%[^:]:%d:%[^:]:%d:%d", 
                  &peer_id, peer_ip, &peer_port, 
                  peer_public_ip, &peer_public_port, &is_public) != 6) {
            continue;
        }
        
        // Skip our own node
        if (peer_id == node->id) {
            continue;
        }
        
        // Check if we already know this peer
        bool known_peer = false;
        
        pthread_mutex_lock(&node->peers_mutex);
        
        for (int i = 0; i < node->peer_count; i++) {
            if (node->peers[i].id == peer_id) {
                known_peer = true;
                break;
            }
        }
        
        pthread_mutex_unlock(&node->peers_mutex);
        
        // Add new peer
        if (!known_peer) {
            printf("Discovered new peer from discovery server: Node %d at %s:%d\n", 
                   peer_id, is_public ? peer_ip : peer_public_ip, 
                   is_public ? peer_port : peer_public_port);
            
            add_peer(node, peer_id, is_public ? peer_ip : peer_public_ip, 
                     is_public ? peer_port : peer_public_port);
            
            // If both nodes are behind NAT, try hole punching
            if (node->is_behind_nat && !is_public) {
                pthread_mutex_lock(&node->peers_mutex);
                
                for (int i = 0; i < node->peer_count; i++) {
                    if (node->peers[i].id == peer_id) {
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
            
            connect_to_node(node, peer_id);
            count++;
        }
    }
    
    fclose(file);
    
    if (count > 0) {
        printf("Discovered %d new peers from discovery server\n", count);
    }
    
    return count;
}

// Discovery server client thread
void* discovery_server_thread(void* arg) {
    Node* node = (Node*)arg;
    
    // Register with discovery server
    register_with_discovery_server(node, DEFAULT_DISCOVERY_SERVER, DEFAULT_DISCOVERY_PORT);
    
    // Query discovery server periodically
    while (discovery_running && node->is_running) {
        query_discovery_server(node, DEFAULT_DISCOVERY_SERVER, DEFAULT_DISCOVERY_PORT);
        
        // Sleep for a while
        sleep(30);
    }
    
    return NULL;
}

// Start discovery server client
int start_discovery_server_client(Node* node, const char* server, int port) {
    if (discovery_running) {
        return 0;  // Already running
    }
    
    discovery_running = true;
    if (pthread_create(&discovery_thread_id, NULL, discovery_server_thread, node) != 0) {
        perror("Failed to create discovery server thread");
        discovery_running = false;
        return -1;
    }
    
    printf("Discovery server client started for node %d\n", node->id);
    return 0;
}

// Stop discovery server client
void stop_discovery_server_client(Node* node) {
    if (!discovery_running) {
        return;
    }
    
    discovery_running = false;
    pthread_join(discovery_thread_id, NULL);
    
    printf("Discovery server client stopped for node %d\n", node->id);
}