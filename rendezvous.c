#include "rendezvous.h"
#include "dht.h"
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// ノードごとのランデブーキー情報（最大10個のキーを管理）
#define MAX_RENDEZVOUS_KEYS 10

// ランデブーデータ構造体
typedef struct {
    RendezvousKeyInfo keys[MAX_RENDEZVOUS_KEYS];
    int key_count;
    pthread_mutex_t mutex;
} RendezvousData;

// ランデブーキーをDHT IDに変換
DhtId rendezvous_key_to_dht_id(const char* key) {
    return dht_generate_id_from_string(key);
}

// ランデブー機能の初期化
int rendezvous_init(Node* node) {
    // ランデブーデータの確保
    RendezvousData* data = (RendezvousData*)malloc(sizeof(RendezvousData));
    if (!data) {
        perror("Failed to allocate rendezvous data");
        return -1;
    }
    
    // 初期化
    memset(data, 0, sizeof(RendezvousData));
    pthread_mutex_init(&data->mutex, NULL);
    
    // ノードのユーザーデータとして保存
    node->rendezvous_data = data;
    
    printf("Rendezvous service initialized for node %d\n", node->id);
    return 0;
}

// ランデブー機能のクリーンアップ
int rendezvous_cleanup(Node* node) {
    if (!node->rendezvous_data) {
        return 0;
    }
    
    RendezvousData* data = (RendezvousData*)node->rendezvous_data;
    
    // 全てのアクティブなランデブーキーから離脱
    pthread_mutex_lock(&data->mutex);
    for (int i = 0; i < data->key_count; i++) {
        if (data->keys[i].active) {
            // DHT上のランデブーポイントから離脱する処理
            // （実際の実装では、DHT上のデータを削除するなどの処理が必要）
        }
    }
    pthread_mutex_unlock(&data->mutex);
    
    // リソースの解放
    pthread_mutex_destroy(&data->mutex);
    free(data);
    node->rendezvous_data = NULL;
    
    printf("Rendezvous service cleaned up for node %d\n", node->id);
    return 0;
}

// ランデブーキーに参加
int rendezvous_join(Node* node, const char* key) {
    if (!node->rendezvous_data || !key || strlen(key) == 0) {
        return -1;
    }
    
    RendezvousData* data = (RendezvousData*)node->rendezvous_data;
    
    pthread_mutex_lock(&data->mutex);
    
    // 既に参加しているか確認
    for (int i = 0; i < data->key_count; i++) {
        if (strcmp(data->keys[i].key, key) == 0) {
            // 既に参加している場合は更新
            data->keys[i].last_used = time(NULL);
            data->keys[i].active = true;
            pthread_mutex_unlock(&data->mutex);
            
            printf("Node %d updated rendezvous key: %s\n", node->id, key);
            
            // DHT上にアナウンス
            DhtId dht_id = rendezvous_key_to_dht_id(key);
            
            // ノード情報をDHTに保存
            char value[256];
            snprintf(value, sizeof(value), "%d,%s,%d,%s,%d,%d", 
                     node->id, node->ip, ntohs(node->addr.sin_port),
                     node->public_ip, node->public_port, node->is_behind_nat ? 1 : 0);
            
            dht_store_value(node, &dht_id, value, strlen(value) + 1);
            
            return 0;
        }
    }
    
    // 新しいキーを追加
    if (data->key_count < MAX_RENDEZVOUS_KEYS) {
        strncpy(data->keys[data->key_count].key, key, MAX_RENDEZVOUS_KEY_LEN - 1);
        data->keys[data->key_count].key[MAX_RENDEZVOUS_KEY_LEN - 1] = '\0';
        data->keys[data->key_count].last_used = time(NULL);
        data->keys[data->key_count].active = true;
        data->key_count++;
        
        pthread_mutex_unlock(&data->mutex);
        
        printf("Node %d joined rendezvous key: %s\n", node->id, key);
        
        // DHT上にアナウンス
        DhtId dht_id = rendezvous_key_to_dht_id(key);
        
        // ノード情報をDHTに保存
        char value[256];
        snprintf(value, sizeof(value), "%d,%s,%d,%s,%d,%d", 
                 node->id, node->ip, ntohs(node->addr.sin_port),
                 node->public_ip, node->public_port, node->is_behind_nat ? 1 : 0);
        
        dht_store_value(node, &dht_id, value, strlen(value) + 1);
        
        return 0;
    }
    
    // キーの上限に達した場合
    pthread_mutex_unlock(&data->mutex);
    printf("Node %d failed to join rendezvous key: too many keys\n", node->id);
    return -1;
}

// ランデブーキーから離脱
int rendezvous_leave(Node* node, const char* key) {
    if (!node->rendezvous_data || !key || strlen(key) == 0) {
        return -1;
    }
    
    RendezvousData* data = (RendezvousData*)node->rendezvous_data;
    
    pthread_mutex_lock(&data->mutex);
    
    // キーを探す
    for (int i = 0; i < data->key_count; i++) {
        if (strcmp(data->keys[i].key, key) == 0) {
            // キーを非アクティブにする
            data->keys[i].active = false;
            pthread_mutex_unlock(&data->mutex);
            
            printf("Node %d left rendezvous key: %s\n", node->id, key);
            
            // DHT上から削除（実際の実装では、DHT上のデータを削除する処理が必要）
            
            return 0;
        }
    }
    
    // キーが見つからない場合
    pthread_mutex_unlock(&data->mutex);
    printf("Node %d failed to leave rendezvous key: key not found\n", node->id);
    return -1;
}

// ランデブーキーに参加しているピアを検索
int rendezvous_find_peers(Node* node, const char* key) {
    if (!node->rendezvous_data || !key || strlen(key) == 0) {
        return -1;
    }
    
    printf("Node %d searching for peers with rendezvous key: %s\n", node->id, key);
    
    // DHT上でキーを検索
    DhtId dht_id = rendezvous_key_to_dht_id(key);
    
    // DHT上のノードを検索
    DhtNodeInfo results[10];
    int count = dht_find_node(node, &dht_id, results, 10);
    
    printf("Found %d DHT nodes closest to rendezvous key\n", count);
    
    // 各ノードに問い合わせ
    for (int i = 0; i < count; i++) {
        // ランデブーメッセージを作成
        RendezvousMessage msg;
        memset(&msg, 0, sizeof(msg));
        
        msg.type = RENDEZVOUS_QUERY;
        msg.node_id = node->id;
        strncpy(msg.rendezvous_key, key, MAX_RENDEZVOUS_KEY_LEN - 1);
        strncpy(msg.ip, node->ip, MAX_IP_STR_LEN - 1);
        msg.port = ntohs(node->addr.sin_port);
        
        if (node->is_behind_nat) {
            strncpy(msg.public_ip, node->public_ip, MAX_IP_STR_LEN - 1);
            msg.public_port = node->public_port;
            msg.is_public = false;
        } else {
            strncpy(msg.public_ip, node->ip, MAX_IP_STR_LEN - 1);
            msg.public_port = ntohs(node->addr.sin_port);
            msg.is_public = true;
        }
        
        msg.timestamp = (uint32_t)time(NULL);
        
        // メッセージを送信
        rendezvous_send_message(node, &msg, results[i].ip, results[i].port);
    }
    
    return count;
}

// ランデブーメッセージの処理
int rendezvous_process_message(Node* node, RendezvousMessage* msg, struct sockaddr_in* sender_addr) {
    if (!node->rendezvous_data || !msg || !sender_addr) {
        return -1;
    }
    
    RendezvousData* data = (RendezvousData*)node->rendezvous_data;
    
    switch (msg->type) {
        case RENDEZVOUS_ANNOUNCE:
            // ランデブーポイントへの参加通知
            printf("Node %d received rendezvous announce from node %d for key %s\n", 
                   node->id, msg->node_id, msg->rendezvous_key);
            
            // DHT上に保存
            {
                DhtId dht_id = rendezvous_key_to_dht_id(msg->rendezvous_key);
                
                char value[256];
                snprintf(value, sizeof(value), "%d,%s,%d,%s,%d,%d", 
                         msg->node_id, msg->ip, msg->port,
                         msg->public_ip, msg->public_port, msg->is_public ? 0 : 1);
                
                dht_store_value(node, &dht_id, value, strlen(value) + 1);
            }
            break;
            
        case RENDEZVOUS_QUERY:
            // ランデブーポイントの検索
            printf("Node %d received rendezvous query from node %d for key %s\n", 
                   node->id, msg->node_id, msg->rendezvous_key);
            
            // 自分がこのキーに参加しているか確認
            pthread_mutex_lock(&data->mutex);
            bool participating = false;
            for (int i = 0; i < data->key_count; i++) {
                if (strcmp(data->keys[i].key, msg->rendezvous_key) == 0 && data->keys[i].active) {
                    participating = true;
                    break;
                }
            }
            pthread_mutex_unlock(&data->mutex);
            
            if (participating) {
                // 応答を送信
                RendezvousMessage response;
                memset(&response, 0, sizeof(response));
                
                response.type = RENDEZVOUS_RESPONSE;
                response.node_id = node->id;
                strncpy(response.rendezvous_key, msg->rendezvous_key, MAX_RENDEZVOUS_KEY_LEN - 1);
                strncpy(response.ip, node->ip, MAX_IP_STR_LEN - 1);
                response.port = ntohs(node->addr.sin_port);
                
                if (node->is_behind_nat) {
                    strncpy(response.public_ip, node->public_ip, MAX_IP_STR_LEN - 1);
                    response.public_port = node->public_port;
                    response.is_public = false;
                } else {
                    strncpy(response.public_ip, node->ip, MAX_IP_STR_LEN - 1);
                    response.public_port = ntohs(node->addr.sin_port);
                    response.is_public = true;
                }
                
                response.timestamp = (uint32_t)time(NULL);
                
                // 送信元に応答
                const char* target_ip = msg->is_public ? msg->ip : msg->public_ip;
                int target_port = msg->is_public ? msg->port : msg->public_port;
                
                rendezvous_send_message(node, &response, target_ip, target_port);
            }
            break;
            
        case RENDEZVOUS_RESPONSE:
            // ランデブーポイントの応答
            printf("Node %d received rendezvous response from node %d for key %s\n", 
                   node->id, msg->node_id, msg->rendezvous_key);
            
            // ピア情報を追加
            {
                NodeInfo peer_info;
                memset(&peer_info, 0, sizeof(peer_info));
                
                peer_info.id = msg->node_id;
                strncpy(peer_info.ip, msg->ip, MAX_IP_STR_LEN - 1);
                peer_info.port = msg->port;
                
                if (!msg->is_public) {
                    strncpy(peer_info.public_ip, msg->public_ip, MAX_IP_STR_LEN - 1);
                    peer_info.public_port = msg->public_port;
                }
                
                // ピアリストに追加
                add_peer_info(node, &peer_info);
                
                // 接続要求を送信
                RendezvousMessage connect_msg;
                memset(&connect_msg, 0, sizeof(connect_msg));
                
                connect_msg.type = RENDEZVOUS_CONNECT;
                connect_msg.node_id = node->id;
                strncpy(connect_msg.rendezvous_key, msg->rendezvous_key, MAX_RENDEZVOUS_KEY_LEN - 1);
                strncpy(connect_msg.ip, node->ip, MAX_IP_STR_LEN - 1);
                connect_msg.port = ntohs(node->addr.sin_port);
                
                if (node->is_behind_nat) {
                    strncpy(connect_msg.public_ip, node->public_ip, MAX_IP_STR_LEN - 1);
                    connect_msg.public_port = node->public_port;
                    connect_msg.is_public = false;
                } else {
                    strncpy(connect_msg.public_ip, node->ip, MAX_IP_STR_LEN - 1);
                    connect_msg.public_port = ntohs(node->addr.sin_port);
                    connect_msg.is_public = true;
                }
                
                connect_msg.timestamp = (uint32_t)time(NULL);
                
                // 送信
                const char* target_ip = msg->is_public ? msg->ip : msg->public_ip;
                int target_port = msg->is_public ? msg->port : msg->public_port;
                
                rendezvous_send_message(node, &connect_msg, target_ip, target_port);
            }
            break;
            
        case RENDEZVOUS_CONNECT:
            // 接続要求
            printf("Node %d received rendezvous connect from node %d for key %s\n", 
                   node->id, msg->node_id, msg->rendezvous_key);
            
            // ピア情報を追加
            {
                NodeInfo peer_info;
                memset(&peer_info, 0, sizeof(peer_info));
                
                peer_info.id = msg->node_id;
                strncpy(peer_info.ip, msg->ip, MAX_IP_STR_LEN - 1);
                peer_info.port = msg->port;
                
                if (!msg->is_public) {
                    strncpy(peer_info.public_ip, msg->public_ip, MAX_IP_STR_LEN - 1);
                    peer_info.public_port = msg->public_port;
                }
                
                // ピアリストに追加
                add_peer_info(node, &peer_info);
                
                // 接続確立のためのメッセージを送信
                // （実際の実装では、NAT越えのための処理が必要）
            }
            break;
            
        default:
            printf("Node %d received unknown rendezvous message type: %d\n", 
                   node->id, msg->type);
            return -1;
    }
    
    return 0;
}

// ランデブーメッセージの送信
int rendezvous_send_message(Node* node, RendezvousMessage* msg, const char* target_ip, int target_port) {
    if (!node || !msg || !target_ip) {
        return -1;
    }
    
    // 送信先アドレスの設定
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_addr.s_addr = inet_addr(target_ip);
    target_addr.sin_port = htons(target_port);
    
    // メッセージの送信
    if (sendto(node->socket_fd, msg, sizeof(RendezvousMessage), 0, 
               (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        perror("Failed to send rendezvous message");
        return -1;
    }
    
    printf("Node %d sent rendezvous message type %d to %s:%d\n", 
           node->id, msg->type, target_ip, target_port);
    
    return 0;
}