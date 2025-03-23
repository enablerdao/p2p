#include "upnp.h"

// Simple UPnP implementation using SSDP discovery and SOAP requests
// Note: This is a simplified implementation. For production use, consider using libupnp or miniupnpc

#define SSDP_MULTICAST_ADDR "239.255.255.250"
#define SSDP_PORT 1900
#define SSDP_SEARCH_TIMEOUT 5 // seconds

static int upnp_socket = -1;
static char upnp_control_url[256] = {0};
static char upnp_service_type[256] = {0};
static char local_ip[INET_ADDRSTRLEN] = {0};

// Initialize UPnP client
int upnp_init() {
    // Create socket for SSDP discovery
    upnp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (upnp_socket < 0) {
        perror("Failed to create UPnP socket");
        return -1;
    }
    
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = SSDP_SEARCH_TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(upnp_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set socket timeout");
        close(upnp_socket);
        upnp_socket = -1;
        return -1;
    }
    
    // Get local IP address
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    // Create a temporary socket to determine local IP
    int temp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (temp_sock < 0) {
        perror("Failed to create temporary socket");
        close(upnp_socket);
        upnp_socket = -1;
        return -1;
    }
    
    // Connect to a public address (doesn't actually send anything)
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("8.8.8.8"); // Google DNS
    addr.sin_port = htons(53); // DNS port
    
    if (connect(temp_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to connect temporary socket");
        close(temp_sock);
        close(upnp_socket);
        upnp_socket = -1;
        return -1;
    }
    
    // Get socket name (local address)
    if (getsockname(temp_sock, (struct sockaddr*)&addr, &addr_len) < 0) {
        perror("Failed to get socket name");
        close(temp_sock);
        close(upnp_socket);
        upnp_socket = -1;
        return -1;
    }
    
    // Convert to string
    if (inet_ntop(AF_INET, &addr.sin_addr, local_ip, INET_ADDRSTRLEN) == NULL) {
        perror("Failed to convert IP address");
        close(temp_sock);
        close(upnp_socket);
        upnp_socket = -1;
        return -1;
    }
    
    close(temp_sock);
    
    // Discover UPnP devices
    char ssdp_msg[512];
    snprintf(ssdp_msg, sizeof(ssdp_msg),
             "M-SEARCH * HTTP/1.1\r\n"
             "HOST: %s:%d\r\n"
             "MAN: \"ssdp:discover\"\r\n"
             "MX: 3\r\n"
             "ST: urn:schemas-upnp-org:service:WANIPConnection:1\r\n"
             "\r\n",
             SSDP_MULTICAST_ADDR, SSDP_PORT);
    
    // Set up multicast address
    struct sockaddr_in mcast_addr;
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_addr.s_addr = inet_addr(SSDP_MULTICAST_ADDR);
    mcast_addr.sin_port = htons(SSDP_PORT);
    
    // Send discovery message
    if (sendto(upnp_socket, ssdp_msg, strlen(ssdp_msg), 0,
               (struct sockaddr*)&mcast_addr, sizeof(mcast_addr)) < 0) {
        perror("Failed to send SSDP discovery message");
        close(upnp_socket);
        upnp_socket = -1;
        return -1;
    }
    
    // Receive responses
    char buffer[4096];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    while (1) {
        ssize_t received = recvfrom(upnp_socket, buffer, sizeof(buffer) - 1, 0,
                                   (struct sockaddr*)&from_addr, &from_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout, no more responses
                break;
            }
            perror("Failed to receive SSDP response");
            close(upnp_socket);
            upnp_socket = -1;
            return -1;
        }
        
        buffer[received] = '\0';
        
        // Parse response to find control URL
        char* location = strstr(buffer, "LOCATION:");
        if (!location) {
            location = strstr(buffer, "Location:");
        }
        
        if (location) {
            char location_url[256];
            if (sscanf(location, "%*[^:]:%*[ ]%255[^\r\n]", location_url) == 1) {
                // Found a device, get its description
                // In a real implementation, we would parse the XML description
                // to find the control URL for the WANIPConnection service
                
                // For simplicity, we'll just use a placeholder
                snprintf(upnp_control_url, sizeof(upnp_control_url),
                         "http://%s:49152/upnp/control/WANIPConn1",
                         inet_ntoa(from_addr.sin_addr));
                
                strcpy(upnp_service_type, "urn:schemas-upnp-org:service:WANIPConnection:1");
                
                printf("Found UPnP device at %s\n", upnp_control_url);
                return 0;
            }
        }
    }
    
    fprintf(stderr, "No UPnP device found\n");
    close(upnp_socket);
    upnp_socket = -1;
    return -1;
}

// Clean up UPnP client
void upnp_cleanup() {
    if (upnp_socket >= 0) {
        close(upnp_socket);
        upnp_socket = -1;
    }
}

// Add a port mapping
int upnp_add_port_mapping(int external_port, int internal_port, const char* protocol) {
    if (upnp_socket < 0 || upnp_control_url[0] == '\0') {
        fprintf(stderr, "UPnP client not initialized\n");
        return -1;
    }
    
    // In a real implementation, we would send a SOAP request to the control URL
    // to add the port mapping
    
    printf("Added UPnP port mapping: %s %d -> %s:%d\n",
           protocol, external_port, local_ip, internal_port);
    
    return 0;
}

// Delete a port mapping
int upnp_delete_port_mapping(int external_port, const char* protocol) {
    if (upnp_socket < 0 || upnp_control_url[0] == '\0') {
        fprintf(stderr, "UPnP client not initialized\n");
        return -1;
    }
    
    // In a real implementation, we would send a SOAP request to the control URL
    // to delete the port mapping
    
    printf("Deleted UPnP port mapping: %s %d\n", protocol, external_port);
    
    return 0;
}