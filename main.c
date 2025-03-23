#include "node.h"
#include <signal.h>

Node* nodes[MAX_NODES];
int num_nodes = 0;
volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void handle_signal(int sig) {
    printf("\nShutting down...\n");
    running = 0;
}

// Initialize the network with a specified number of nodes
void init_network(int count) {
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
        num_nodes++;
    }

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

    printf("Network initialized with %d nodes\n", num_nodes);
}

// Clean up all nodes
void cleanup_network() {
    for (int i = 0; i < num_nodes; i++) {
        if (nodes[i]) {
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

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -n COUNT   Number of nodes to create (default: 10)\n");
    printf("  -h         Display this help message\n");
}

int main(int argc, char* argv[]) {
    int node_count = 10;
    int opt;
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "n:h")) != -1) {
        switch (opt) {
            case 'n':
                node_count = atoi(optarg);
                if (node_count <= 0 || node_count > MAX_NODES) {
                    fprintf(stderr, "Invalid node count. Must be between 1 and %d.\n", MAX_NODES);
                    return 1;
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
    
    // Initialize network
    init_network(node_count);
    
    // Run demo messaging
    demo_messaging();
    
    // Keep running until signal received
    printf("Network running. Press Ctrl+C to exit.\n");
    while (running) {
        sleep(1);
    }
    
    // Clean up
    cleanup_network();
    printf("Network shutdown complete.\n");
    
    return 0;
}