#include "dht.h"
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

// DHT関連のグローバル変数
static pthread_t dht_thread;
static bool dht_running = false;

// ノードにDHTデータを追加するための拡張
typedef struct {
    RoutingTable routing_table;
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

// DHT初期化
void dht_init(Node* node) {
    // DHT用のデータ構造を確保
    DhtData* dht_data = (DhtData*)malloc(sizeof(DhtData));
    if (!dht_data) {
        perror("Failed to allocate DHT data");
        return;
    }
    
    // 初期化
    memset(dht_data, 0, sizeof(DhtData));
    pthread_mutex_init(&dht_data->dht_mutex, NULL);
    
    // ノードIDからDHT IDを生成
    char id_str[64];
    snprintf(id_str, sizeof(id_str), "node-%d-%s-%d", node->id, node->ip, ntohs(node->addr.sin_port));
    dht_data->routing_table.self_id = dht_generate_id_from_string(id_str);
    
    // ノードにDHTデータを関連付ける（実際のNode構造体にDHTデータへのポインタを追加する必要がある）
    // この例では、ノードのユーザーデータとして保存する方法を示す
    node->dht_data = dht_data;
    
    // DHT IDを表示
    char hex_id[DHT_ID_BITS/4 + 1];
    dht_id_to_hex(&dht_data->routing_table.self_id, hex_id, sizeof(hex_id));
    printf("Node %d initialized with DHT ID: %s\n", node->id, hex_id);
    
    // メンテナンススレッドを開始
    dht_running = true;
    if (pthread_create(&dht_thread, NULL, dht_maintenance_thread, node) != 0) {
        perror("Failed to create DHT maintenance thread");
        dht_running = false;
    }
}

// DHT終了処理
void dht_cleanup(Node* node) {
    if (!node->dht_data) {
        return;
    }
    
    // メンテナンススレッドを停止
    dht_running = false;
    pthread_join(dht_thread, NULL);
    
    // DHT用のデータ構造を解放
    DhtData* dht_data = (DhtData*)node->dht_data;
    pthread_mutex_destroy(&dht_data->dht_mutex);
    free(dht_data);
    node->dht_data = NULL;
    
    printf("DHT cleaned up for node %d\n", node->id);
}

// ランダムなDHT IDを生成
DhtId dht_generate_id() {
    DhtId id;
    RAND_bytes(id.bytes, sizeof(id.bytes));
    return id;
}

// 文字列からDHT IDを生成（SHA-1ハッシュを使用）
DhtId dht_generate_id_from_string(const char* str) {
    DhtId id;
    SHA1((const unsigned char*)str, strlen(str), id.bytes);
    return id;
}

// 2つのDHT ID間の距離（XORメトリック）
int dht_id_distance(const DhtId* id1, const DhtId* id2) {
    // XORの結果の最上位ビットを見つける（最初に1になるビット）
    for (int i = 0; i < DHT_ID_BITS/8; i++) {
        uint8_t xor_result = id1->bytes[i] ^ id2->bytes[i];
        if (xor_result == 0) {
            continue;
        }
        
        // 最上位ビットを見つける
        for (int j = 7; j >= 0; j--) {
            if ((xor_result & (1 << j)) != 0) {
                return i * 8 + (7 - j);
            }
        }
    }
    
    // 完全に同じIDの場合
    return DHT_ID_BITS;
}

// ルーティングテーブルにノードを追加
void dht_add_node(Node* node, const DhtNodeInfo* dht_node) {
    if (!node->dht_data) {
        return;
    }
    
    DhtData* dht_data = (DhtData*)node->dht_data;
    pthread_mutex_lock(&dht_data->dht_mutex);
    
    // 自分自身は追加しない
    if (memcmp(dht_node->id.bytes, dht_data->routing_table.self_id.bytes, DHT_ID_BITS/8) == 0) {
        pthread_mutex_unlock(&dht_data->dht_mutex);
        return;
    }
    
    // IDの距離を計算して適切なバケットを見つける
    int bucket_idx = dht_id_distance(&dht_data->routing_table.self_id, &dht_node->id);
    if (bucket_idx >= DHT_ID_BITS) {
        pthread_mutex_unlock(&dht_data->dht_mutex);
        return;  // 同じIDは追加しない
    }
    
    KBucket* bucket = &dht_data->routing_table.buckets[bucket_idx];
    
    // すでに存在するか確認
    for (int i = 0; i < bucket->count; i++) {
        if (memcmp(bucket->nodes[i].id.bytes, dht_node->id.bytes, DHT_ID_BITS/8) == 0) {
            // 既存のノードを更新
            strncpy(bucket->nodes[i].ip, dht_node->ip, MAX_IP_STR_LEN - 1);
            bucket->nodes[i].ip[MAX_IP_STR_LEN - 1] = '\0';
            bucket->nodes[i].port = dht_node->port;
            bucket->nodes[i].last_seen = time(NULL);
            bucket->last_updated = time(NULL);
            pthread_mutex_unlock(&dht_data->dht_mutex);
            return;
        }
    }
    
    // バケットに空きがあれば追加
    if (bucket->count < DHT_K) {
        bucket->nodes[bucket->count] = *dht_node;
        bucket->nodes[bucket->count].last_seen = time(NULL);
        bucket->count++;
        bucket->last_updated = time(NULL);
        
        char hex_id[DHT_ID_BITS/4 + 1];
        dht_id_to_hex(&dht_node->id, hex_id, sizeof(hex_id));
        printf("Added DHT node %s at %s:%d to bucket %d\n", 
               hex_id, dht_node->ip, dht_node->port, bucket_idx);
    } else {
        // バケットが満杯の場合、最も古いノードを置き換えるか、pingを送信して生存確認
        // この簡易実装では、最も古いノードを置き換える
        int oldest_idx = 0;
        time_t oldest_time = bucket->nodes[0].last_seen;
        
        for (int i = 1; i < bucket->count; i++) {
            if (bucket->nodes[i].last_seen < oldest_time) {
                oldest_idx = i;
                oldest_time = bucket->nodes[i].last_seen;
            }
        }
        
        // 一定時間経過していれば置き換え
        if (time(NULL) - oldest_time > 3600) {  // 1時間以上経過
            bucket->nodes[oldest_idx] = *dht_node;
            bucket->nodes[oldest_idx].last_seen = time(NULL);
            bucket->last_updated = time(NULL);
            
            char hex_id[DHT_ID_BITS/4 + 1];
            dht_id_to_hex(&dht_node->id, hex_id, sizeof(hex_id));
            printf("Replaced old DHT node with %s at %s:%d in bucket %d\n", 
                   hex_id, dht_node->ip, dht_node->port, bucket_idx);
        }
    }
    
    pthread_mutex_unlock(&dht_data->dht_mutex);
}

// 指定したIDに最も近いノードを見つける
int dht_find_node(Node* node, const DhtId* target_id, DhtNodeInfo* result, int max_results) {
    if (!node->dht_data || max_results <= 0) {
        return 0;
    }
    
    DhtData* dht_data = (DhtData*)node->dht_data;
    pthread_mutex_lock(&dht_data->dht_mutex);
    
    // 結果を格納する配列
    typedef struct {
        DhtNodeInfo info;
        int distance;
    } NodeDistance;
    
    NodeDistance* distances = (NodeDistance*)malloc(sizeof(NodeDistance) * DHT_K * DHT_ID_BITS);
    if (!distances) {
        pthread_mutex_unlock(&dht_data->dht_mutex);
        return 0;
    }
    
    int total_nodes = 0;
    
    // すべてのバケットからノードを収集
    for (int i = 0; i < DHT_ID_BITS; i++) {
        KBucket* bucket = &dht_data->routing_table.buckets[i];
        for (int j = 0; j < bucket->count; j++) {
            distances[total_nodes].info = bucket->nodes[j];
            distances[total_nodes].distance = dht_id_distance(target_id, &bucket->nodes[j].id);
            total_nodes++;
        }
    }
    
    // 距離でソート（XORメトリックが小さい順）
    for (int i = 0; i < total_nodes - 1; i++) {
        for (int j = 0; j < total_nodes - i - 1; j++) {
            if (distances[j].distance > distances[j + 1].distance) {
                NodeDistance temp = distances[j];
                distances[j] = distances[j + 1];
                distances[j + 1] = temp;
            }
        }
    }
    
    // 結果を返す（最大max_results個）
    int result_count = (total_nodes < max_results) ? total_nodes : max_results;
    for (int i = 0; i < result_count; i++) {
        result[i] = distances[i].info;
    }
    
    free(distances);
    pthread_mutex_unlock(&dht_data->dht_mutex);
    
    return result_count;
}

// 値を保存
int dht_store_value(Node* node, const DhtId* key, const void* value, size_t value_len) {
    if (!node->dht_data || !value || value_len > MAX_BUFFER) {
        return -1;
    }
    
    DhtData* dht_data = (DhtData*)node->dht_data;
    pthread_mutex_lock(&dht_data->dht_mutex);
    
    // 既存のキーを探す
    for (int i = 0; i < dht_data->storage_count; i++) {
        if (dht_data->storage[i].in_use && 
            memcmp(dht_data->storage[i].key.bytes, key->bytes, DHT_ID_BITS/8) == 0) {
            // 既存のキーを更新
            memcpy(dht_data->storage[i].value, value, value_len);
            dht_data->storage[i].value_len = value_len;
            pthread_mutex_unlock(&dht_data->dht_mutex);
            return 0;
        }
    }
    
    // 新しいキーを追加
    if (dht_data->storage_count < 100) {
        dht_data->storage[dht_data->storage_count].key = *key;
        memcpy(dht_data->storage[dht_data->storage_count].value, value, value_len);
        dht_data->storage[dht_data->storage_count].value_len = value_len;
        dht_data->storage[dht_data->storage_count].in_use = true;
        dht_data->storage_count++;
        pthread_mutex_unlock(&dht_data->dht_mutex);
        return 0;
    }
    
    // ストレージが満杯
    pthread_mutex_unlock(&dht_data->dht_mutex);
    return -1;
}

// 値を検索
int dht_find_value(Node* node, const DhtId* key, void* value, size_t* value_len) {
    if (!node->dht_data || !value || !value_len) {
        return -1;
    }
    
    DhtData* dht_data = (DhtData*)node->dht_data;
    pthread_mutex_lock(&dht_data->dht_mutex);
    
    // キーを探す
    for (int i = 0; i < dht_data->storage_count; i++) {
        if (dht_data->storage[i].in_use && 
            memcmp(dht_data->storage[i].key.bytes, key->bytes, DHT_ID_BITS/8) == 0) {
            // キーが見つかった
            size_t copy_len = (*value_len < dht_data->storage[i].value_len) ? 
                              *value_len : dht_data->storage[i].value_len;
            memcpy(value, dht_data->storage[i].value, copy_len);
            *value_len = copy_len;
            pthread_mutex_unlock(&dht_data->dht_mutex);
            return 0;
        }
    }
    
    // キーが見つからない
    pthread_mutex_unlock(&dht_data->dht_mutex);
    return -1;
}

// バケットの更新（定期的に呼び出される）
void dht_refresh_buckets(Node* node) {
    if (!node->dht_data) {
        return;
    }
    
    DhtData* dht_data = (DhtData*)node->dht_data;
    pthread_mutex_lock(&dht_data->dht_mutex);
    
    time_t now = time(NULL);
    
    // 各バケットを確認
    for (int i = 0; i < DHT_ID_BITS; i++) {
        KBucket* bucket = &dht_data->routing_table.buckets[i];
        
        // 一定時間更新されていないバケットを更新
        if (bucket->count > 0 && now - bucket->last_updated > DHT_REFRESH_INTERVAL) {
            // ランダムなIDを生成してそのバケットに対応するIDを作成
            DhtId random_id = dht_data->routing_table.self_id;
            
            // i番目のビットを反転
            int byte_idx = i / 8;
            int bit_idx = i % 8;
            random_id.bytes[byte_idx] ^= (1 << (7 - bit_idx));
            
            // このIDに近いノードを探す（実際のネットワークでは他のノードに問い合わせる）
            // この簡易実装では、ローカルのルーティングテーブルのみを使用
            
            bucket->last_updated = now;
        }
        
        // 古いノードを削除
        int j = 0;
        while (j < bucket->count) {
            if (now - bucket->nodes[j].last_seen > DHT_REFRESH_INTERVAL * 2) {
                // 古いノードを削除
                for (int k = j; k < bucket->count - 1; k++) {
                    bucket->nodes[k] = bucket->nodes[k + 1];
                }
                bucket->count--;
            } else {
                j++;
            }
        }
    }
    
    pthread_mutex_unlock(&dht_data->dht_mutex);
}

// DHT メンテナンススレッド
void* dht_maintenance_thread(void* arg) {
    Node* node = (Node*)arg;
    
    while (dht_running && node->is_running) {
        // バケットの更新
        dht_refresh_buckets(node);
        
        // 一定時間待機
        sleep(60);  // 1分ごとに更新
    }
    
    return NULL;
}

// DHT IDを16進数文字列に変換
void dht_id_to_hex(const DhtId* id, char* hex, size_t hex_len) {
    if (hex_len < DHT_ID_BITS/4 + 1) {
        if (hex_len > 0) {
            hex[0] = '\0';
        }
        return;
    }
    
    for (int i = 0; i < DHT_ID_BITS/8; i++) {
        snprintf(hex + i*2, 3, "%02x", id->bytes[i]);
    }
}

// 16進数文字列からDHT IDに変換
int dht_hex_to_id(const char* hex, DhtId* id) {
    if (strlen(hex) < DHT_ID_BITS/4) {
        return -1;
    }
    
    for (int i = 0; i < DHT_ID_BITS/8; i++) {
        sscanf(hex + i*2, "%2hhx", &id->bytes[i]);
    }
    
    return 0;
}

// DHT IDを表示
void dht_print_id(const DhtId* id) {
    char hex[DHT_ID_BITS/4 + 1];
    dht_id_to_hex(id, hex, sizeof(hex));
    printf("%s", hex);
}