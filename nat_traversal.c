#include "node.h"
#include "stun.h"
#include "upnp.h"
#include "firewall.h"

// Enable NAT traversal for a node
int node_enable_nat_traversal(Node* node, const char* stun_server) {
    printf("Enabling NAT traversal for node %d using STUN server %s\n", node->id, stun_server);
    
    // Initialize STUN client
    if (stun_init() < 0) {
        fprintf(stderr, "Failed to initialize STUN client\n");
        return -1;
    }
    
    // Discover NAT type and public IP/port
    StunResult* result = stun_discover_nat(stun_server);
    if (!result) {
        fprintf(stderr, "Failed to discover NAT using STUN\n");
        stun_cleanup();
        return -1;
    }
    
    // Store public IP and port
    strncpy(node->public_ip, result->public_ip, MAX_IP_STR_LEN - 1);
    node->public_ip[MAX_IP_STR_LEN - 1] = '\0';
    node->public_port = result->public_port;
    node->is_behind_nat = true;
    
    printf("\n==================================================\n");
    printf("Node %d is behind NAT\n", node->id);
    printf("Public address: %s:%d\n", node->public_ip, node->public_port);
    printf("To connect to this node from another computer, use:\n");
    printf("  ./node_network -p %d:%s:%d\n", node->id, node->public_ip, node->public_port);
    printf("==================================================\n");
    
    free(result);
    
    // Try to set up UPnP port forwarding if enabled
    if (node->use_upnp) {
        node_enable_upnp(node);
    }
    
    return 0;
}

// Enable UPnP port forwarding
int node_enable_upnp(Node* node) {
    printf("Enabling UPnP for node %d\n", node->id);
    
    // Initialize UPnP client
    if (upnp_init() < 0) {
        fprintf(stderr, "Failed to initialize UPnP client\n");
        return -1;
    }
    
    // Add port mapping
    int local_port = ntohs(node->addr.sin_port);
    if (upnp_add_port_mapping(local_port, local_port, "UDP") < 0) {
        fprintf(stderr, "Failed to add UPnP port mapping\n");
        upnp_cleanup();
        return -1;
    }
    
    printf("UPnP port mapping added for node %d: %d -> %s:%d\n", 
           node->id, local_port, node->ip, local_port);
    
    return 0;
}

// Perform NAT hole punching to establish direct connection
int node_punch_hole(Node* from_node, NodeInfo* peer) {
    printf("Attempting to punch hole to node %d at %s:%d\n", 
           peer->id, peer->public_ip, peer->public_port);
    
    // Create a dummy message to punch a hole in the NAT
    ProtocolMessage msg;
    msg.type = MSG_TYPE_NAT_TRAVERSAL;
    msg.seq = 0;
    msg.from_id = from_node->id;
    msg.to_id = peer->id;
    msg.data_len = 0;
    
    // Set up destination address
    struct sockaddr_in to_addr;
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_addr.s_addr = inet_addr(peer->public_ip);
    to_addr.sin_port = htons(peer->public_port);
    
    // Send multiple packets to increase chance of success
    for (int i = 0; i < 5; i++) {
        sendto(from_node->socket_fd, &msg, sizeof(msg), 0,
               (struct sockaddr*)&to_addr, sizeof(to_addr));
        usleep(100000); // 100ms
    }
    
    printf("NAT hole punching completed for node %d\n", peer->id);
    return 0;
}

// Share peer list with another node
void node_share_peer_list(Node* node, int to_id) {
    // Prepare peer list data
    char peer_data[MAX_BUFFER];
    int offset = 0;
    
    pthread_mutex_lock(&node->peers_mutex);
    
    // Format: count,id:ip:port:public_ip:public_port:is_public,...
    offset += snprintf(peer_data + offset, MAX_BUFFER - offset, "%d,", node->peer_count);
    
    for (int i = 0; i < node->peer_count && offset < MAX_BUFFER; i++) {
        NodeInfo* peer = &node->peers[i];
        
        // Skip the recipient node
        if (peer->id == to_id) {
            continue;
        }
        
        offset += snprintf(peer_data + offset, MAX_BUFFER - offset, 
                          "%d:%s:%d:%s:%d:%d,",
                          peer->id, peer->ip, peer->port,
                          peer->public_ip, peer->public_port,
                          peer->is_public ? 1 : 0);
    }
    
    pthread_mutex_unlock(&node->peers_mutex);
    
    // Remove trailing comma
    if (offset > 0 && peer_data[offset - 1] == ',') {
        peer_data[offset - 1] = '\0';
    }
    
    // Send peer list
    send_protocol_message(node, to_id, MSG_TYPE_PEER_LIST, peer_data, strlen(peer_data));
    
    printf("Shared peer list with node %d\n", to_id);
}

// Process received peer list
void node_process_peer_list(Node* node, const char* peer_data) {
    int count;
    const char* p = peer_data;
    
    // Parse count
    if (sscanf(p, "%d,", &count) != 1) {
        fprintf(stderr, "Invalid peer list format\n");
        return;
    }
    
    // Skip count
    p = strchr(p, ',');
    if (!p) {
        return;
    }
    p++;
    
    // Parse peers
    for (int i = 0; i < count; i++) {
        int peer_id;
        char peer_ip[MAX_IP_STR_LEN];
        int peer_port;
        char peer_public_ip[MAX_IP_STR_LEN];
        int peer_public_port;
        int is_public;
        
        if (sscanf(p, "%d:%[^:]:%d:%[^:]:%d:%d,", 
                  &peer_id, peer_ip, &peer_port, 
                  peer_public_ip, &peer_public_port, &is_public) != 6) {
            break;
        }
        
        // Skip to next peer
        p = strchr(p, ',');
        if (!p) {
            break;
        }
        p++;
        
        // Check if we already know this peer
        bool known_peer = false;
        
        pthread_mutex_lock(&node->peers_mutex);
        
        for (int j = 0; j < node->peer_count; j++) {
            if (node->peers[j].id == peer_id) {
                known_peer = true;
                break;
            }
        }
        
        pthread_mutex_unlock(&node->peers_mutex);
        
        // Add new peer
        if (!known_peer) {
            printf("Discovered new peer from peer list: Node %d at %s:%d\n", 
                   peer_id, is_public ? peer_ip : peer_public_ip, 
                   is_public ? peer_port : peer_public_port);
            
            add_peer(node, peer_id, is_public ? peer_ip : peer_public_ip, 
                     is_public ? peer_port : peer_public_port);
            
            // If both nodes are behind NAT, try hole punching
            if (node->is_behind_nat && !is_public) {
                pthread_mutex_lock(&node->peers_mutex);
                
                for (int j = 0; j < node->peer_count; j++) {
                    if (node->peers[j].id == peer_id) {
                        node_punch_hole(node, &node->peers[j]);
                        break;
                    }
                }
                
                pthread_mutex_unlock(&node->peers_mutex);
            }
            
            connect_to_node(node, peer_id);
        }
    }
}

// Maintain peer connections
void node_maintain_peers(Node* node) {
    time_t now = time(NULL);
    
    pthread_mutex_lock(&node->peers_mutex);
    
    // Check for stale peers
    for (int i = 0; i < node->peer_count; i++) {
        // If we haven't seen this peer for 5 minutes, remove it
        if (now - node->peers[i].last_seen > 300) {
            printf("Removing stale peer: Node %d\n", node->peers[i].id);
            
            // Remove peer by swapping with the last one
            if (i < node->peer_count - 1) {
                node->peers[i] = node->peers[node->peer_count - 1];
            }
            node->peer_count--;
            i--; // Recheck this index
        }
    }
    
    pthread_mutex_unlock(&node->peers_mutex);
}