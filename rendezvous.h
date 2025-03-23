#ifndef RENDEZVOUS_H
#define RENDEZVOUS_H

#include "node.h"
#include "dht.h"

// ランデブーキーの最大長
#define MAX_RENDEZVOUS_KEY_LEN 64

// ランデブーメッセージタイプ
typedef enum {
    RENDEZVOUS_ANNOUNCE = 1,  // ランデブーポイントへの参加通知
    RENDEZVOUS_QUERY,         // ランデブーポイントの検索
    RENDEZVOUS_RESPONSE,      // ランデブーポイントの応答
    RENDEZVOUS_CONNECT        // 接続要求
} RendezvousMessageType;

// ランデブーメッセージ
typedef struct {
    RendezvousMessageType type;
    int node_id;
    char rendezvous_key[MAX_RENDEZVOUS_KEY_LEN];
    char ip[MAX_IP_STR_LEN];
    int port;
    char public_ip[MAX_IP_STR_LEN];
    int public_port;
    bool is_public;
    uint32_t timestamp;
} RendezvousMessage;

// ランデブーキーの情報
typedef struct {
    char key[MAX_RENDEZVOUS_KEY_LEN];
    time_t last_used;
    bool active;
} RendezvousKeyInfo;

// ランデブー関数プロトタイプ
int rendezvous_init(Node* node);
int rendezvous_cleanup(Node* node);
int rendezvous_join(Node* node, const char* key);
int rendezvous_leave(Node* node, const char* key);
int rendezvous_find_peers(Node* node, const char* key);
int rendezvous_process_message(Node* node, RendezvousMessage* msg, struct sockaddr_in* sender_addr);
int rendezvous_send_message(Node* node, RendezvousMessage* msg, const char* target_ip, int target_port);

// ユーティリティ関数
DhtId rendezvous_key_to_dht_id(const char* key);

#endif /* RENDEZVOUS_H */