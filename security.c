#include "security.h"

// Simple HMAC implementation (for demonstration purposes)
// In a real application, use a cryptographic library like OpenSSL
void generate_hmac(const char* data, size_t data_len, const char* key, size_t key_len, uint8_t* hmac) {
    // Simple XOR-based HMAC for demonstration
    // NOT SECURE - use a proper HMAC implementation in production
    
    // Initialize HMAC with zeros
    memset(hmac, 0, HMAC_SIZE);
    
    // XOR data with key
    for (size_t i = 0; i < data_len; i++) {
        hmac[i % HMAC_SIZE] ^= data[i] ^ key[i % key_len];
    }
    
    // Additional mixing
    for (int i = 0; i < HMAC_SIZE; i++) {
        hmac[i] = (hmac[i] << 3) | (hmac[i] >> 5);
        hmac[i] ^= key[(i * 7) % key_len];
    }
}

// Verify HMAC
int verify_hmac(const char* data, size_t data_len, const char* key, size_t key_len, const uint8_t* hmac) {
    uint8_t computed_hmac[HMAC_SIZE];
    generate_hmac(data, data_len, key, key_len, computed_hmac);
    
    // Compare HMACs
    return memcmp(hmac, computed_hmac, HMAC_SIZE) == 0 ? 1 : 0;
}

// Generate a random key
void generate_random_key(char* key, size_t key_len) {
    FILE* urandom = fopen("/dev/urandom", "r");
    if (urandom) {
        fread(key, 1, key_len, urandom);
        fclose(urandom);
    } else {
        // Fallback if /dev/urandom is not available
        srand(time(NULL));
        for (size_t i = 0; i < key_len; i++) {
            key[i] = rand() & 0xFF;
        }
    }
}