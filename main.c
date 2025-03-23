#include "node.h"
#include "stun.h"
#include "upnp.h"
#include "discovery.h"
#include <signal.h>
#include <getopt.h>

Node* nodes[MAX_NODES];
int num_nodes = 0;
volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void handle_signal(int sig) {
    printf("\nShutting down...\n");
    running = 0;
}

// Initialize the network with a specified number of nodes
void init_network(int count, bool use_nat_traversal, bool use_upnp, bool use_discovery, const char* stun_server) {
    if (count > MAX_NODES) {
        printf("Warning: Maximum number of nodes is %d. Using that instead.\n", MAX_NODES);
        count = MAX_NODES;
    }

    // Get local IP address
    char* local_ip = get_local_ip();
    if (!local_ip) {
        fprintf(stderr, "Failed to get local IP address, using 127.0.0.1\n");
        local_ip = "127.0.0.1";
    }
    
    printf("Using local IP address: %s\n", local_ip);

    // Create nodes
    for (int i = 0; i < count; i++) {
        nodes[i] = create_node(i, local_ip, BASE_PORT + i);
        if (!nodes[i]) {
            fprintf(stderr, "Failed to create node %d\n", i);
            continue;
        }
        
        // Initialize mutex
        pthread_mutex_init(&nodes[i]->peers_mutex, NULL);
        
        // Set NAT traversal options
        nodes[i]->use_upnp = use_upnp;
        nodes[i]->use_discovery = use_discovery;
        
        // Enable NAT traversal if requested
        if (use_nat_traversal) {
            node_enable_nat_traversal(nodes[i], stun_server);
        }
        
        // Enable UPnP if requested and not already enabled by NAT traversal
        if (use_upnp && !use_nat_traversal) {
            node_enable_upnp(nodes[i]);
        }
        
        num_nodes++;
    }

    // If not using discovery, manually connect nodes
    if (!use_discovery) {
        // Add all nodes as peers to each other
        for (int i = 0; i < num_nodes; i++) {
            for (int j = 0; j < num_nodes; j++) {
                if (i != j) {
                    add_peer(nodes[i], j, local_ip, BASE_PORT + j);
                }
            }
        }

        // Connect all nodes to each other
        for (int i = 0; i < num_nodes; i++) {
            for (int j = 0; j < num_nodes; j++) {
                if (i != j) {
                    connect_to_node(nodes[i], j);
                }
            }
        }
    } else {
        // Enable discovery for all nodes
        for (int i = 0; i < num_nodes; i++) {
            discovery_init(nodes[i]);
        }
    }

    printf("Network initialized with %d nodes\n", num_nodes);
}

// Clean up all nodes
void cleanup_network() {
    // Clean up discovery service if used
    discovery_cleanup();
    
    // Clean up UPnP if used
    upnp_cleanup();
    
    // Clean up STUN if used
    stun_cleanup();
    
    // Clean up nodes
    for (int i = 0; i < num_nodes; i++) {
        if (nodes[i]) {
            // Remove UPnP port mappings
            if (nodes[i]->use_upnp) {
                upnp_delete_port_mapping(BASE_PORT + i, "UDP");
            }
            
            // Destroy mutex
            pthread_mutex_destroy(&nodes[i]->peers_mutex);
            
            // Destroy node
            destroy_node(nodes[i]);
            nodes[i] = NULL;
        }
    }
    num_nodes = 0;
}

// Demonstrate sending messages between nodes
void demo_messaging() {
    // Send some test messages
    for (int i = 0; i < num_nodes; i++) {
        for (int j = 0; j < num_nodes; j++) {
            if (i != j) {
                char message[MAX_BUFFER];
                snprintf(message, MAX_BUFFER, "Hello from node %d to node %d!", i, j);
                send_message(nodes[i], j, message);
                
                // Small delay to avoid flooding
                usleep(100000); // 100ms
            }
        }
    }
}

// Maintain the network (keep connections alive, remove stale peers, etc.)
void maintain_network() {
    for (int i = 0; i < num_nodes; i++) {
        node_maintain_peers(nodes[i]);
    }
}

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -n COUNT       Number of nodes to create (default: 10)\n");
    printf("  -t             Enable NAT traversal using STUN\n");
    printf("  -u             Enable UPnP port forwarding\n");
    printf("  -d             Enable automatic peer discovery\n");
    printf("  -s SERVER      STUN server to use (default: stun.l.google.com:19302)\n");
    printf("  -p PEER        Add a remote peer (format: id:ip:port)\n");
    printf("  -h             Display this help message\n");
}

int main(int argc, char* argv[]) {
    int node_count = 10;
    bool use_nat_traversal = false;
    bool use_upnp = false;
    bool use_discovery = false;
    char stun_server[256] = "stun.l.google.com";
    int opt;
    
    // Remote peers to add
    struct {
        int id;
        char ip[MAX_IP_STR_LEN];
        int port;
    } remote_peers[MAX_NODES];
    int remote_peer_count = 0;
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "n:tuds:p:h")) != -1) {
        switch (opt) {
            case 'n':
                node_count = atoi(optarg);
                if (node_count <= 0 || node_count > MAX_NODES) {
                    fprintf(stderr, "Invalid node count. Must be between 1 and %d.\n", MAX_NODES);
                    return 1;
                }
                break;
            case 't':
                use_nat_traversal = true;
                break;
            case 'u':
                use_upnp = true;
                break;
            case 'd':
                use_discovery = true;
                break;
            case 's':
                strncpy(stun_server, optarg, sizeof(stun_server) - 1);
                stun_server[sizeof(stun_server) - 1] = '\0';
                break;
            case 'p':
                if (remote_peer_count < MAX_NODES) {
                    if (sscanf(optarg, "%d:%[^:]:%d", 
                              &remote_peers[remote_peer_count].id,
                              remote_peers[remote_peer_count].ip,
                              &remote_peers[remote_peer_count].port) == 3) {
                        remote_peer_count++;
                    } else {
                        fprintf(stderr, "Invalid peer format. Use id:ip:port\n");
                    }
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Set up signal handler
    signal(SIGINT, handle_signal);
    
    printf("Starting node network with %d nodes...\n", node_count);
    printf("NAT traversal: %s\n", use_nat_traversal ? "enabled" : "disabled");
    printf("UPnP: %s\n", use_upnp ? "enabled" : "disabled");
    printf("Automatic discovery: %s\n", use_discovery ? "enabled" : "disabled");
    if (use_nat_traversal) {
        printf("STUN server: %s\n", stun_server);
    }
    
    // Initialize network
    init_network(node_count, use_nat_traversal, use_upnp, use_discovery, stun_server);
    
    // Add remote peers if specified
    if (remote_peer_count > 0) {
        printf("Adding %d remote peers...\n", remote_peer_count);
        
        for (int i = 0; i < remote_peer_count; i++) {
            printf("Adding remote peer: Node %d at %s:%d\n", 
                   remote_peers[i].id, remote_peers[i].ip, remote_peers[i].port);
            
            // Add the remote peer to all local nodes
            for (int j = 0; j < num_nodes; j++) {
                add_peer(nodes[j], remote_peers[i].id, remote_peers[i].ip, remote_peers[i].port);
                connect_to_node(nodes[j], remote_peers[i].id);
                
                // If using NAT traversal, try to punch a hole
                if (use_nat_traversal && nodes[j]->is_behind_nat) {
                    pthread_mutex_lock(&nodes[j]->peers_mutex);
                    
                    for (int k = 0; k < nodes[j]->peer_count; k++) {
                        if (nodes[j]->peers[k].id == remote_peers[i].id) {
                            node_punch_hole(nodes[j], &nodes[j]->peers[k]);
                            break;
                        }
                    }
                    
                    pthread_mutex_unlock(&nodes[j]->peers_mutex);
                }
                
                // Share our peer list with the remote peer
                node_share_peer_list(nodes[j], remote_peers[i].id);
            }
        }
    }
    
    // Run demo messaging
    demo_messaging();
    
    // Keep running until signal received
    printf("Network running. Press Ctrl+C to exit.\n");
    
    time_t last_maintenance = time(NULL);
    
    while (running) {
        sleep(1);
        
        // Perform maintenance every 60 seconds
        time_t now = time(NULL);
        if (now - last_maintenance >= 60) {
            maintain_network();
            last_maintenance = now;
        }
    }
    
    // Clean up
    cleanup_network();
    printf("Network shutdown complete.\n");
    
    return 0;
}