#ifndef UPNP_H
#define UPNP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

// Function prototypes
int upnp_init();
int upnp_add_port_mapping(int external_port, int internal_port, const char* protocol);
int upnp_delete_port_mapping(int external_port, const char* protocol);
void upnp_cleanup();

#endif /* UPNP_H */