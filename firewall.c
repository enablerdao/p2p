#include "firewall.h"
#include "node.h"

// Common firewall-allowed ports
// These ports are commonly allowed through firewalls
const int FIREWALL_FRIENDLY_PORTS[FW_PORT_COUNT] = {
    80,    // HTTP
    443,   // HTTPS
    8080,  // Alternative HTTP
    8443,  // Alternative HTTPS
    21,    // FTP
    22,    // SSH
    25,    // SMTP
    53,    // DNS
    123,   // NTP
    5223   // Apple Push Notification
};

// Try to bind to firewall-friendly ports
int try_firewall_friendly_ports(Node* node, int base_port) {
    printf("Trying firewall-friendly ports for node %d\n", node->id);
    
    // First try the base port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(base_port);
    
    if (bind(node->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        printf("Successfully bound to base port %d\n", base_port);
        node->addr = addr;
        return base_port;
    }
    
    // If base port fails, try firewall-friendly ports
    for (int i = 0; i < FW_PORT_COUNT; i++) {
        int port = FIREWALL_FRIENDLY_PORTS[i];
        
        // Skip if port is already in use
        addr.sin_port = htons(port);
        if (bind(node->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            printf("Successfully bound to firewall-friendly port %d\n", port);
            node->addr = addr;
            return port;
        }
    }
    
    // If all else fails, try random high ports
    for (int i = 0; i < 10; i++) {
        int port = 10000 + (rand() % 50000);
        addr.sin_port = htons(port);
        if (bind(node->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            printf("Successfully bound to random port %d\n", port);
            node->addr = addr;
            return port;
        }
    }
    
    printf("Failed to bind to any port\n");
    return -1;
}

// Punch holes through firewall on multiple ports
int punch_multiple_ports(Node* from_node, NodeInfo* peer) {
    printf("Attempting to punch holes on multiple ports to node %d at %s\n", 
           peer->id, peer->public_ip);
    
    // Create a dummy message to punch a hole in the NAT
    ProtocolMessage msg;
    msg.type = MSG_TYPE_NAT_TRAVERSAL;
    msg.seq = 0;
    msg.from_id = from_node->id;
    msg.to_id = peer->id;
    msg.data_len = 0;
    
    // Try the peer's known port first
    struct sockaddr_in to_addr;
    memset(&to_addr, 0, sizeof(to_addr));
    to_addr.sin_family = AF_INET;
    to_addr.sin_addr.s_addr = inet_addr(peer->public_ip);
    to_addr.sin_port = htons(peer->port);
    
    // Send multiple packets to increase chance of success
    for (int i = 0; i < 3; i++) {
        sendto(from_node->socket_fd, &msg, sizeof(msg), 0,
               (struct sockaddr*)&to_addr, sizeof(to_addr));
        usleep(100000); // 100ms
    }
    
    // Try firewall-friendly ports
    for (int i = 0; i < FW_PORT_COUNT; i++) {
        to_addr.sin_port = htons(FIREWALL_FRIENDLY_PORTS[i]);
        
        // Send multiple packets to each port
        for (int j = 0; j < 2; j++) {
            sendto(from_node->socket_fd, &msg, sizeof(msg), 0,
                   (struct sockaddr*)&to_addr, sizeof(to_addr));
            usleep(50000); // 50ms
        }
    }
    
    printf("Firewall hole punching completed for node %d\n", peer->id);
    return 0;
}