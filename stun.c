#include "stun.h"

static int stun_socket = -1;

// Initialize STUN client
int stun_init() {
    stun_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (stun_socket < 0) {
        perror("Failed to create STUN socket");
        return -1;
    }
    
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(stun_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set socket timeout");
        close(stun_socket);
        stun_socket = -1;
        return -1;
    }
    
    return 0;
}

// Clean up STUN client
void stun_cleanup() {
    if (stun_socket >= 0) {
        close(stun_socket);
        stun_socket = -1;
    }
}

// Create a STUN binding request
static void create_stun_request(uint8_t* request, size_t* request_size) {
    StunHeader* header = (StunHeader*)request;
    
    // Fill header
    header->message_type = htons(STUN_BINDING_REQUEST);
    header->message_length = htons(0); // No attributes
    header->magic_cookie = htonl(STUN_MAGIC_COOKIE);
    
    // Generate random transaction ID
    for (int i = 0; i < 12; i++) {
        header->transaction_id[i] = rand() % 256;
    }
    
    *request_size = STUN_HEADER_SIZE;
}

// Parse STUN response to extract mapped address
static int parse_stun_response(const uint8_t* response, size_t response_size, StunResult* result) {
    if (response_size < STUN_HEADER_SIZE) {
        fprintf(stderr, "STUN response too short\n");
        return -1;
    }
    
    const StunHeader* header = (const StunHeader*)response;
    
    // Check message type
    uint16_t msg_type = ntohs(header->message_type);
    if (msg_type != STUN_BINDING_RESPONSE) {
        fprintf(stderr, "Not a STUN binding response: %04x\n", msg_type);
        return -1;
    }
    
    // Check magic cookie
    if (ntohl(header->magic_cookie) != STUN_MAGIC_COOKIE) {
        fprintf(stderr, "Invalid magic cookie\n");
        return -1;
    }
    
    // Parse attributes
    const uint8_t* attr = response + STUN_HEADER_SIZE;
    size_t attr_size = response_size - STUN_HEADER_SIZE;
    
    while (attr_size >= 4) {
        uint16_t attr_type = ntohs(*(uint16_t*)attr);
        uint16_t attr_length = ntohs(*((uint16_t*)attr + 1));
        
        if (attr_size < 4 + attr_length) {
            break;
        }
        
        const uint8_t* attr_value = attr + 4;
        
        if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
            if (attr_length >= 8) { // IPv4 XOR-MAPPED-ADDRESS
                uint8_t family = attr_value[1];
                if (family == 0x01) { // IPv4
                    uint16_t port = ntohs(*(uint16_t*)(attr_value + 2));
                    uint32_t ip = ntohl(*(uint32_t*)(attr_value + 4));
                    
                    // XOR with magic cookie
                    port ^= (STUN_MAGIC_COOKIE >> 16);
                    ip ^= STUN_MAGIC_COOKIE;
                    
                    // Convert to string
                    struct in_addr addr;
                    addr.s_addr = htonl(ip);
                    inet_ntop(AF_INET, &addr, result->public_ip, INET_ADDRSTRLEN);
                    result->public_port = port;
                    
                    return 0;
                }
            }
        } else if (attr_type == STUN_ATTR_MAPPED_ADDRESS) {
            if (attr_length >= 8) { // IPv4 MAPPED-ADDRESS
                uint8_t family = attr_value[1];
                if (family == 0x01) { // IPv4
                    uint16_t port = ntohs(*(uint16_t*)(attr_value + 2));
                    uint32_t ip = ntohl(*(uint32_t*)(attr_value + 4));
                    
                    // Convert to string
                    struct in_addr addr;
                    addr.s_addr = htonl(ip);
                    inet_ntop(AF_INET, &addr, result->public_ip, INET_ADDRSTRLEN);
                    result->public_port = port;
                    
                    return 0;
                }
            }
        }
        
        attr += 4 + ((attr_length + 3) & ~3); // Move to next attribute (with padding)
        attr_size -= 4 + ((attr_length + 3) & ~3);
    }
    
    fprintf(stderr, "No mapped address found in STUN response\n");
    return -1;
}

// Discover public IP and port using STUN
StunResult* stun_discover_nat(const char* stun_server) {
    if (stun_socket < 0) {
        fprintf(stderr, "STUN client not initialized\n");
        return NULL;
    }
    
    // Resolve STUN server
    struct hostent* server = gethostbyname(stun_server);
    if (server == NULL) {
        fprintf(stderr, "Failed to resolve STUN server: %s\n", stun_server);
        return NULL;
    }
    
    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(STUN_PORT);
    
    // Create STUN request
    uint8_t request[STUN_HEADER_SIZE];
    size_t request_size;
    create_stun_request(request, &request_size);
    
    // Send request
    if (sendto(stun_socket, request, request_size, 0, 
               (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to send STUN request");
        return NULL;
    }
    
    // Receive response
    uint8_t response[1024];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(stun_socket, response, sizeof(response), 0,
                               (struct sockaddr*)&from_addr, &from_len);
    
    if (received < 0) {
        perror("Failed to receive STUN response");
        return NULL;
    }
    
    // Parse response
    StunResult* result = (StunResult*)malloc(sizeof(StunResult));
    if (!result) {
        perror("Failed to allocate memory for STUN result");
        return NULL;
    }
    
    if (parse_stun_response(response, received, result) < 0) {
        free(result);
        return NULL;
    }
    
    return result;
}