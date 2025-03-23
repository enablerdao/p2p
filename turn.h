#ifndef TURN_H
#define TURN_H

#include "node.h"

// 前方宣言
typedef struct TurnData TurnData;

// TURNサーバーの設定
#define TURN_DEFAULT_PORT 3478
#define TURN_MAX_BUFFER 1500
#define TURN_ALLOCATION_LIFETIME 600  // 10分（秒単位）

// TURNメッセージタイプ
typedef enum {
    TURN_ALLOCATION_REQUEST = 0x0003,
    TURN_ALLOCATION_RESPONSE = 0x0103,
    TURN_ALLOCATION_ERROR_RESPONSE = 0x0113,
    TURN_REFRESH_REQUEST = 0x0004,
    TURN_REFRESH_RESPONSE = 0x0104,
    TURN_REFRESH_ERROR_RESPONSE = 0x0114,
    TURN_SEND_INDICATION = 0x0016,
    TURN_DATA_INDICATION = 0x0017,
    TURN_CREATE_PERMISSION_REQUEST = 0x0008,
    TURN_CREATE_PERMISSION_RESPONSE = 0x0108,
    TURN_CREATE_PERMISSION_ERROR_RESPONSE = 0x0118,
    TURN_CHANNEL_BIND_REQUEST = 0x0009,
    TURN_CHANNEL_BIND_RESPONSE = 0x0109,
    TURN_CHANNEL_BIND_ERROR_RESPONSE = 0x0119
} TurnMessageType;

// TURNメッセージ属性タイプ
typedef enum {
    TURN_ATTR_MAPPED_ADDRESS = 0x0001,
    TURN_ATTR_XOR_MAPPED_ADDRESS = 0x0020,
    TURN_ATTR_USERNAME = 0x0006,
    TURN_ATTR_MESSAGE_INTEGRITY = 0x0008,
    TURN_ATTR_ERROR_CODE = 0x0009,
    TURN_ATTR_REALM = 0x0014,
    TURN_ATTR_NONCE = 0x0015,
    TURN_ATTR_XOR_RELAYED_ADDRESS = 0x0016,
    TURN_ATTR_REQUESTED_TRANSPORT = 0x0019,
    TURN_ATTR_LIFETIME = 0x000D,
    TURN_ATTR_DATA = 0x0013,
    TURN_ATTR_XOR_PEER_ADDRESS = 0x0012,
    TURN_ATTR_CHANNEL_NUMBER = 0x000C
} TurnAttributeType;

// TURNクライアント状態
typedef enum {
    TURN_STATE_IDLE,
    TURN_STATE_ALLOCATING,
    TURN_STATE_ALLOCATED,
    TURN_STATE_FAILED
} TurnClientState;

// TURNクライアント構造体
typedef struct {
    char server[MAX_IP_STR_LEN];
    int port;
    char username[64];
    char password[64];
    char realm[128];
    char nonce[128];
    int socket_fd;
    struct sockaddr_in server_addr;
    char relayed_ip[MAX_IP_STR_LEN];
    int relayed_port;
    TurnClientState state;
    time_t allocation_expiry;
    pthread_t refresh_thread;
    bool refresh_running;
    pthread_mutex_t mutex;
} TurnClient;

// TURNクライアントデータ
struct TurnData {
    TurnClient client;
};

// 関数プロトタイプ
int turn_init(Node* node, const char* server, int port, const char* username, const char* password);
int turn_cleanup(Node* node);
int turn_allocate(Node* node);
int turn_refresh(Node* node, int lifetime);
int turn_create_permission(Node* node, const char* peer_ip);
int turn_send_data(Node* node, const char* peer_ip, int peer_port, const void* data, int data_len);
int turn_process_data(Node* node, const void* data, int data_len, char* from_ip, int* from_port);
void* turn_refresh_thread(void* arg);

#endif /* TURN_H */