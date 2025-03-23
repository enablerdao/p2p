#ifndef DHT_H
#define DHT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "node.h"

// DHT設定
#define DHT_ID_BITS 160  // SHA-1ハッシュを使用
#define DHT_K 8          // k-bucketのサイズ
#define DHT_ALPHA 3      // 並列ルックアップの数
#define DHT_REFRESH_INTERVAL 3600  // バケット更新間隔（秒）

// DHT ID（SHA-1ハッシュ、160ビット）
typedef struct {
    uint8_t bytes[DHT_ID_BITS/8];
} DhtId;

// DHT データ構造体
typedef struct {
    struct RoutingTable* routing_table;
    pthread_mutex_t dht_mutex;
    
    // 値の保存用ハッシュテーブル（簡易実装）
    struct {
        DhtId key;
        uint8_t value[MAX_BUFFER];
        size_t value_len;
        bool in_use;
    } storage[100];  // 最大100個の値を保存
    int storage_count;
} DhtData;

// DHT ノード情報
typedef struct {
    DhtId id;                    // ノードのDHT ID
    char ip[MAX_IP_STR_LEN];     // IPアドレス
    int port;                    // ポート
    time_t last_seen;            // 最後に見た時間
} DhtNodeInfo;

// k-bucket
typedef struct {
    DhtNodeInfo nodes[DHT_K];    // バケット内のノード
    int count;                   // ノード数
    time_t last_updated;         // 最後に更新された時間
} KBucket;

// DHT ルーティングテーブル
typedef struct RoutingTable {
    KBucket buckets[DHT_ID_BITS]; // 各ビット位置に対応するバケット
    DhtId self_id;                // 自分のID
} RoutingTable;

// DHT メッセージタイプ
typedef enum {
    DHT_PING = 1,
    DHT_PONG,
    DHT_FIND_NODE,
    DHT_FIND_NODE_REPLY,
    DHT_FIND_VALUE,
    DHT_FIND_VALUE_REPLY,
    DHT_STORE
} DhtMessageType;

// DHT メッセージ
typedef struct {
    DhtMessageType type;         // メッセージタイプ
    DhtId sender_id;             // 送信者ID
    DhtId target_id;             // 対象ID（検索対象など）
    uint32_t transaction_id;     // トランザクションID
    uint16_t data_len;           // データ長
    char data[MAX_BUFFER];       // データ
} DhtMessage;

// DHT 関数プロトタイプ
void dht_init(Node* node);
void dht_cleanup(Node* node);
DhtId dht_generate_id();
DhtId dht_generate_id_from_string(const char* str);
int dht_id_distance(const DhtId* id1, const DhtId* id2);
void dht_add_node(Node* node, const DhtNodeInfo* dht_node);
int dht_find_node(Node* node, const DhtId* target_id, DhtNodeInfo* result, int max_results);
int dht_store_value(Node* node, const DhtId* key, const void* value, size_t value_len);
int dht_find_value(Node* node, const DhtId* key, void* value, size_t* value_len);
void dht_refresh_buckets(Node* node);
void* dht_maintenance_thread(void* arg);

// ユーティリティ関数
void dht_id_to_hex(const DhtId* id, char* hex, size_t hex_len);
int dht_hex_to_id(const char* hex, DhtId* id);
void dht_print_id(const DhtId* id);

#endif /* DHT_H */