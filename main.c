#include "node.h"
#include "stun.h"
#include "upnp.h"
#include "discovery.h"
#include "discovery_server.h"
#include "enhanced_discovery.h"
#include "firewall.h"
#include "reliability.h"
#include "security.h"
#include "diagnostics.h"
#include "dht.h"
#include <signal.h>
#include <getopt.h>
#include <fcntl.h>

Node* nodes[MAX_NODES];
int num_nodes = 0;
volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void handle_signal(int sig) {
    (void)sig; // 未使用パラメータの警告を抑制
    printf("\nShutting down...\n");
    running = 0;
}

// Initialize the network with a specified number of nodes
void init_network(int count, bool use_nat_traversal, bool use_upnp, bool use_discovery, 
                 bool use_discovery_server, bool use_enhanced_discovery, bool use_firewall_bypass, 
                 const char* stun_server, const char* discovery_server, int discovery_port) {
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
        // ノードIDをランダムに生成（0-999の範囲）
        int random_id = rand() % 1000;
        nodes[i] = create_node(random_id, local_ip, BASE_PORT + i);
        if (!nodes[i]) {
            fprintf(stderr, "Failed to create node %d\n", random_id);
            continue;
        }
        
        // Initialize mutex
        pthread_mutex_init(&nodes[i]->peers_mutex, NULL);
        
        // Set options
        nodes[i]->use_upnp = use_upnp;
        nodes[i]->use_discovery = use_discovery;
        nodes[i]->use_discovery_server = use_discovery_server;
        nodes[i]->firewall_bypass = use_firewall_bypass;
        
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
    
    // Enable discovery server client for all nodes
    if (use_discovery_server) {
        for (int i = 0; i < num_nodes; i++) {
            start_discovery_server_client(nodes[i], discovery_server, discovery_port);
        }
    }
    
    // Enable enhanced discovery for all nodes
    if (use_enhanced_discovery) {
        for (int i = 0; i < num_nodes; i++) {
            enhanced_discovery_init(nodes[i]);
        }
    }
    
    // Start reliability service for all nodes
    for (int i = 0; i < num_nodes; i++) {
        start_reliability_service(nodes[i]);
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
            // Clean up DHT if used
            if (use_dht) {
                dht_cleanup(nodes[i]);
            }
            
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
    printf("  -n COUNT       Number of nodes to create (default: 5)\n");
    printf("  -T             Disable NAT traversal (enabled by default)\n");
    printf("  -U             Disable UPnP port forwarding (enabled by default)\n");
    printf("  -D             Disable automatic peer discovery (enabled by default)\n");
    printf("  -E             Disable enhanced peer discovery (enabled by default)\n");
    printf("  -S             Disable discovery server (disabled by default)\n");
    printf("  -F             Disable firewall bypass mode (enabled by default)\n");
    printf("  -s SERVER      STUN server to use (default: stun.l.google.com)\n");
    printf("  -d SERVER:PORT Discovery server to use (default: %s:%d)\n", 
           DEFAULT_DISCOVERY_SERVER, DEFAULT_DISCOVERY_PORT);
    printf("  -p PEER        Add a remote peer (format: id:ip:port)\n");
    printf("  -f             Explicitly enable firewall bypass mode (enabled by default)\n");
    printf("  -h             Display this help message\n");
    printf("\nEnhanced discovery is enabled by default, which allows automatic peer discovery without a central server.\n");
    printf("Use capital letters to disable features (e.g., -T to disable NAT traversal).\n");
    printf("\nInteractive commands available during runtime:\n");
    printf("  status         Show status of all nodes\n");
    printf("  list, nodes    List all nodes and peers\n");
    printf("  ping <id>      Ping a specific node\n");
    printf("  send <id> <msg> Send a message to a specific node\n");
    printf("  diag           Run network diagnostics\n");
    printf("  help           Show help message\n");
    printf("  exit, quit     Exit the program\n");
}

int main(int argc, char* argv[]) {
    int node_count = 5; // デフォルトで5ノード
    bool use_nat_traversal = true;  // デフォルトで有効化
    bool use_upnp = true;           // デフォルトで有効化
    bool use_discovery = true;      // デフォルトで有効化
    bool use_discovery_server = false; // デフォルトで無効化
    bool use_enhanced_discovery = true; // デフォルトで有効化
    bool use_firewall_bypass = true; // デフォルトで有効化
    bool use_dht = true;             // デフォルトでDHT有効化
    bool disable_nat_traversal = false;
    bool disable_upnp = false;
    bool disable_discovery = false;
    bool disable_discovery_server = false;
    bool disable_enhanced_discovery = false;
    bool disable_firewall_bypass = false;
    bool disable_dht = false;
    char stun_server[256] = "stun.l.google.com";
    char discovery_server[256] = DEFAULT_DISCOVERY_SERVER;
    int discovery_port = DEFAULT_DISCOVERY_PORT;
    int opt;
    
    // Remote peers to add
    struct {
        int id;
        char ip[MAX_IP_STR_LEN];
        int port;
    } remote_peers[MAX_NODES];
    int remote_peer_count = 0;
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "n:TUDFSEHs:d:p:hf")) != -1) {
        switch (opt) {
            case 'n':
                node_count = atoi(optarg);
                if (node_count <= 0 || node_count > MAX_NODES) {
                    fprintf(stderr, "Invalid node count. Must be between 1 and %d.\n", MAX_NODES);
                    return 1;
                }
                break;
            case 'T':  // 大文字は無効化を意味する
                disable_nat_traversal = true;
                break;
            case 'U':
                disable_upnp = true;
                break;
            case 'D':
                disable_discovery = true;
                break;
            case 'S':  // ディスカバリーサーバーを無効化
                disable_discovery_server = true;
                break;
            case 'E':  // 拡張ディスカバリーを無効化
                disable_enhanced_discovery = true;
                break;
            case 'F':  // ファイアウォール対策を無効化
                disable_firewall_bypass = true;
                break;
            case 'H':  // DHT機能を無効化
                disable_dht = true;
                break;
            case 'd':  // ディスカバリーサーバーを指定
                {
                    char *server = strtok(optarg, ":");
                    char *port_str = strtok(NULL, ":");
                    
                    if (server) {
                        strncpy(discovery_server, server, sizeof(discovery_server) - 1);
                        discovery_server[sizeof(discovery_server) - 1] = '\0';
                    }
                    
                    if (port_str) {
                        discovery_port = atoi(port_str);
                    }
                }
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
            case 'f':  // ファイアウォール対策モードを明示的に有効化（デフォルトでも有効）
                use_firewall_bypass = true;
                printf("Firewall bypass mode enabled. Will try multiple ports.\n");
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // 無効化フラグが設定されていれば機能をオフにする
    if (disable_nat_traversal) use_nat_traversal = false;
    if (disable_upnp) use_upnp = false;
    if (disable_discovery) use_discovery = false;
    if (disable_discovery_server) use_discovery_server = false;
    if (disable_enhanced_discovery) use_enhanced_discovery = false;
    if (disable_firewall_bypass) use_firewall_bypass = false;
    if (disable_dht) use_dht = false;
    
    // Set up signal handler
    signal(SIGINT, handle_signal);
    
    printf("\n\033[1;33m"); // Bold yellow text
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│ P2P NODE NETWORK                                    │\n");
    printf("├─────────────────────────────────────────────────────┤\n");
    printf("│ Starting node network with %2d nodes                 │\n", node_count);
    printf("│ NAT traversal:       %s                      │\n", use_nat_traversal ? "ENABLED " : "DISABLED");
    printf("│ UPnP:                %s                      │\n", use_upnp ? "ENABLED " : "DISABLED");
    printf("│ Automatic discovery: %s                      │\n", use_discovery ? "ENABLED " : "DISABLED");
    printf("│ Enhanced discovery:  %s                      │\n", use_enhanced_discovery ? "ENABLED " : "DISABLED");
    printf("│ Discovery server:    %s                      │\n", use_discovery_server ? "ENABLED " : "DISABLED");
    printf("│ Firewall bypass:     %s                      │\n", use_firewall_bypass ? "ENABLED " : "DISABLED");
    printf("│ DHT:                %s                      │\n", use_dht ? "ENABLED " : "DISABLED");
    if (use_nat_traversal) {
        printf("│ STUN server:         %-30s │\n", stun_server);
    }
    if (use_discovery_server) {
        printf("│ Discovery server:    %-30s │\n", discovery_server);
        printf("│ Discovery port:      %-30d │\n", discovery_port);
    }
    printf("└─────────────────────────────────────────────────────┘\n");
    printf("\033[0m"); // Reset text formatting
    
    // Initialize network
    init_network(node_count, use_nat_traversal, use_upnp, use_discovery, 
                use_discovery_server, use_enhanced_discovery, use_firewall_bypass, 
                stun_server, discovery_server, discovery_port);
                
    // Initialize DHT for all nodes if enabled
    if (use_dht) {
        for (int i = 0; i < num_nodes; i++) {
            dht_init(nodes[i]);
        }
    }
    
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
    
    // Run diagnostics
    if (num_nodes > 0) {
        run_network_diagnostics(nodes[0]);
    }
    
    // Run demo messaging
    demo_messaging();
    
    // Keep running until signal received
    printf("\n=== Network running. Press Ctrl+C to exit. ===\n");
    printf("Available commands:\n");
    printf("  status       - Show status of all nodes\n");
    printf("  list, nodes  - List all nodes and peers\n");
    printf("  ping <id>    - Ping a specific node\n");
    printf("  send <id> <message> - Send a message to a specific node\n");
    printf("  diag         - Run network diagnostics\n");
    printf("  help         - Show help message\n");
    printf("  exit, quit   - Exit the program\n");
    
    time_t last_maintenance = time(NULL);
    
    // Set up stdin for non-blocking reads
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    char cmd_buffer[256];
    
    while (running) {
        // Check for user commands
        if (fgets(cmd_buffer, sizeof(cmd_buffer), stdin) != NULL) {
            cmd_buffer[strcspn(cmd_buffer, "\n")] = 0; // Remove newline
            
            if (strcmp(cmd_buffer, "status") == 0) {
                // Show status of all nodes
                for (int i = 0; i < num_nodes; i++) {
                    print_node_status(nodes[i]);
                    print_peer_status(nodes[i]);
                }
            } else if (strncmp(cmd_buffer, "ping ", 5) == 0) {
                // Ping a specific node
                int peer_id = atoi(cmd_buffer + 5);
                if (num_nodes > 0) {
                    ping_peer(nodes[0], peer_id, PING_TIMEOUT);
                }
            } else if (strncmp(cmd_buffer, "send ", 5) == 0) {
                // Send a message to a specific node
                char *id_str = cmd_buffer + 5;
                char *msg = strchr(id_str, ' ');
                
                if (msg) {
                    *msg = '\0'; // Split the string
                    msg++; // Move to the message content
                    
                    int peer_id = atoi(id_str);
                    if (num_nodes > 0 && strlen(msg) > 0) {
                        // Check if the peer exists
                        bool peer_exists = false;
                        pthread_mutex_lock(&nodes[0]->peers_mutex);
                        for (int i = 0; i < nodes[0]->peer_count; i++) {
                            if (nodes[0]->peers[i].id == peer_id) {
                                peer_exists = true;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&nodes[0]->peers_mutex);
                        
                        if (peer_exists) {
                            if (send_message(nodes[0], peer_id, msg) == 0) {
                                // Message sent successfully (already printed by send_message)
                            } else {
                                printf("\033[1;31mFailed to send message\033[0m\n");
                            }
                        } else {
                            printf("\033[1;31mPeer node %d not found. Use 'list' to see available peers.\033[0m\n", peer_id);
                        }
                    } else {
                        printf("\033[1;31mInvalid node ID or empty message\033[0m\n");
                    }
                } else {
                    printf("Usage: send <id> <message>\n");
                }
            } else if (strcmp(cmd_buffer, "list") == 0 || strcmp(cmd_buffer, "nodes") == 0) {
                // List all nodes
                printf("Local nodes:\n");
                for (int i = 0; i < num_nodes; i++) {
                    printf("  Node %d: %s:%d\n", nodes[i]->id, nodes[i]->ip, 
                           ntohs(nodes[i]->addr.sin_port));
                    
                    // DHT情報を表示（DHT有効時）
                    if (use_dht && nodes[i]->dht_data) {
                        DhtData* dht_data = (DhtData*)nodes[i]->dht_data;
                        char hex_id[DHT_ID_BITS/4 + 1];
                        dht_id_to_hex(&dht_data->routing_table.self_id, hex_id, sizeof(hex_id));
                        printf("    DHT ID: %s\n", hex_id);
                    }
                }
                
                // List all known remote peers
                if (num_nodes > 0) {
                    print_peer_status(nodes[0]);
                }
            } else if (strcmp(cmd_buffer, "diag") == 0 || strcmp(cmd_buffer, "diagnostics") == 0) {
                // Run diagnostics
                if (num_nodes > 0) {
                    run_network_diagnostics(nodes[0]);
                }
            } else if (strncmp(cmd_buffer, "dht", 3) == 0) {
                // DHT関連のコマンド
                if (!use_dht) {
                    printf("DHT is not enabled. Use -H option to enable it.\n");
                } else {
                    char *subcmd = cmd_buffer + 4; // "dht "の後の部分
                    
                    if (strncmp(subcmd, "find ", 5) == 0) {
                        // DHT検索コマンド
                        char *key_str = subcmd + 5;
                        if (strlen(key_str) > 0) {
                            DhtId key = dht_generate_id_from_string(key_str);
                            DhtNodeInfo results[10];
                            int count = dht_find_node(nodes[0], &key, results, 10);
                            
                            printf("Found %d nodes closest to key: ", count);
                            char hex_key[DHT_ID_BITS/4 + 1];
                            dht_id_to_hex(&key, hex_key, sizeof(hex_key));
                            printf("%s\n", hex_key);
                            
                            for (int i = 0; i < count; i++) {
                                char hex_id[DHT_ID_BITS/4 + 1];
                                dht_id_to_hex(&results[i].id, hex_id, sizeof(hex_id));
                                printf("  %d. %s at %s:%d\n", i+1, hex_id, results[i].ip, results[i].port);
                            }
                        } else {
                            printf("Usage: dht find <key>\n");
                        }
                    } else {
                        printf("Unknown DHT command. Available commands:\n");
                        printf("  dht find <key> - Find nodes closest to a key\n");
                    }
                }
            } else if (strcmp(cmd_buffer, "help") == 0) {
                printf("Available commands:\n");
                printf("  status       - Show status of all nodes\n");
                printf("  list, nodes  - List all nodes and peers\n");
                printf("  ping <id>    - Ping a specific node\n");
                printf("  send <id> <message> - Send a message to a specific node\n");
                printf("  diag         - Run network diagnostics\n");
                printf("  dht find <key> - Find nodes closest to a key in DHT\n");
                printf("  help         - Show this help message\n");
                printf("  exit, quit   - Exit the program\n");
            } else if (strcmp(cmd_buffer, "exit") == 0 || strcmp(cmd_buffer, "quit") == 0) {
                running = 0;
            } else if (strlen(cmd_buffer) > 0) {
                printf("Unknown command: %s\n", cmd_buffer);
                printf("Type 'help' for available commands\n");
            }
        }
        
        // Perform maintenance every 60 seconds
        time_t now = time(NULL);
        if (now - last_maintenance >= 60) {
            maintain_network();
            last_maintenance = now;
        }
        
        usleep(100000); // 100ms
    }
    
    // Clean up
    cleanup_network();
    printf("Network shutdown complete.\n");
    
    return 0;
}