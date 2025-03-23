#ifndef SECURITY_H
#define SECURITY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Security settings
#define HMAC_KEY_SIZE 32
#define HMAC_SIZE 16

// Function prototypes
void generate_hmac(const char* data, size_t data_len, const char* key, size_t key_len, uint8_t* hmac);
int verify_hmac(const char* data, size_t data_len, const char* key, size_t key_len, const uint8_t* hmac);
void generate_random_key(char* key, size_t key_len);

#endif /* SECURITY_H */