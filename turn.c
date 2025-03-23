#include "turn.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <pthread.h>

// TURNクライアントデータ
typedef struct {
    TurnClient client;
} TurnData;

// TURNメッセージヘッダ
typedef struct {
    uint16_t message_type;
    uint16_t message_length;
    uint32_t magic_cookie;
    uint8_t transaction_id[12];
} TurnMessageHeader;

// TURNメッセージ属性ヘッダ
typedef struct {
    uint16_t type;
    uint16_t length;
} TurnAttributeHeader;

// TURNクライアントの初期化
int turn_init(Node* node, const char* server, int port, const char* username, const char* password) {
    if (!node || !server || !username || !password) {
        return -1;
    }
    
    // TURNデータの確保
    TurnData* turn_data = (TurnData*)malloc(sizeof(TurnData));
    if (!turn_data) {
        perror("Failed to allocate TURN data");
        return -1;
    }
    
    // 初期化
    memset(turn_data, 0, sizeof(TurnData));
    
    // クライアント情報の設定
    strncpy(turn_data->client.server, server, MAX_IP_STR_LEN - 1);
    turn_data->client.port = port;
    strncpy(turn_data->client.username, username, sizeof(turn_data->client.username) - 1);
    strncpy(turn_data->client.password, password, sizeof(turn_data->client.password) - 1);
    turn_data->client.state = TURN_STATE_IDLE;
    
    // ソケットの作成
    turn_data->client.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (turn_data->client.socket_fd < 0) {
        perror("Failed to create TURN socket");
        free(turn_data);
        return -1;
    }
    
    // サーバーアドレスの設定
    memset(&turn_data->client.server_addr, 0, sizeof(turn_data->client.server_addr));
    turn_data->client.server_addr.sin_family = AF_INET;
    turn_data->client.server_addr.sin_addr.s_addr = inet_addr(server);
    turn_data->client.server_addr.sin_port = htons(port);
    
    // ミューテックスの初期化
    pthread_mutex_init(&turn_data->client.mutex, NULL);
    
    // ノードにTURNデータを関連付ける
    node->turn_data = turn_data;
    
    printf("TURN client initialized for node %d using server %s:%d\n", 
           node->id, server, port);
    
    return 0;
}

// TURNクライアントのクリーンアップ
int turn_cleanup(Node* node) {
    if (!node || !node->turn_data) {
        return -1;
    }
    
    TurnData* turn_data = (TurnData*)node->turn_data;
    
    // リフレッシュスレッドの停止
    if (turn_data->client.refresh_running) {
        turn_data->client.refresh_running = false;
        pthread_join(turn_data->client.refresh_thread, NULL);
    }
    
    // ソケットのクローズ
    if (turn_data->client.socket_fd >= 0) {
        close(turn_data->client.socket_fd);
    }
    
    // ミューテックスの破棄
    pthread_mutex_destroy(&turn_data->client.mutex);
    
    // メモリの解放
    free(turn_data);
    node->turn_data = NULL;
    
    printf("TURN client cleaned up for node %d\n", node->id);
    
    return 0;
}

// トランザクションIDの生成
static void generate_transaction_id(uint8_t* transaction_id) {
    for (int i = 0; i < 12; i++) {
        transaction_id[i] = rand() % 256;
    }
}

// TURNメッセージの送信
static int send_turn_message(TurnClient* client, uint16_t message_type, 
                            const void* attributes, uint16_t attributes_length) {
    // メッセージバッファ
    uint8_t buffer[TURN_MAX_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    
    // ヘッダの設定
    TurnMessageHeader* header = (TurnMessageHeader*)buffer;
    header->message_type = htons(message_type);
    header->message_length = htons(attributes_length);
    header->magic_cookie = htonl(0x2112A442);  // STUN/TURN magic cookie
    generate_transaction_id(header->transaction_id);
    
    // 属性のコピー
    if (attributes && attributes_length > 0) {
        memcpy(buffer + sizeof(TurnMessageHeader), attributes, attributes_length);
    }
    
    // メッセージの送信
    int total_length = sizeof(TurnMessageHeader) + attributes_length;
    if (sendto(client->socket_fd, buffer, total_length, 0, 
               (struct sockaddr*)&client->server_addr, sizeof(client->server_addr)) < 0) {
        perror("Failed to send TURN message");
        return -1;
    }
    
    return 0;
}

// TURNメッセージの受信
static int receive_turn_message(TurnClient* client, uint8_t* buffer, int buffer_size, 
                               uint16_t* message_type, uint16_t* message_length) {
    // タイムアウトの設定
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (setsockopt(client->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Failed to set socket timeout");
    }
    
    // メッセージの受信
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int received = recvfrom(client->socket_fd, buffer, buffer_size, 0, 
                           (struct sockaddr*)&from_addr, &from_len);
    
    if (received < sizeof(TurnMessageHeader)) {
        if (received < 0) {
            perror("Failed to receive TURN message");
        }
        return -1;
    }
    
    // ヘッダの解析
    TurnMessageHeader* header = (TurnMessageHeader*)buffer;
    *message_type = ntohs(header->message_type);
    *message_length = ntohs(header->message_length);
    
    return received;
}

// TURNアロケーションの要求
int turn_allocate(Node* node) {
    if (!node || !node->turn_data) {
        return -1;
    }
    
    TurnData* turn_data = (TurnData*)node->turn_data;
    TurnClient* client = &turn_data->client;
    
    pthread_mutex_lock(&client->mutex);
    
    // 既にアロケーション済みの場合
    if (client->state == TURN_STATE_ALLOCATED) {
        pthread_mutex_unlock(&client->mutex);
        return 0;
    }
    
    // アロケーション要求の属性
    uint8_t attributes[256];
    int attr_offset = 0;
    
    // Requested Transport属性（UDPを指定）
    TurnAttributeHeader* transport_attr = (TurnAttributeHeader*)(attributes + attr_offset);
    transport_attr->type = htons(TURN_ATTR_REQUESTED_TRANSPORT);
    transport_attr->length = htons(4);
    attr_offset += sizeof(TurnAttributeHeader);
    
    // UDPプロトコル（17）を指定
    attributes[attr_offset++] = 17;  // UDP
    attributes[attr_offset++] = 0;   // Reserved
    attributes[attr_offset++] = 0;   // Reserved
    attributes[attr_offset++] = 0;   // Reserved
    
    // Username属性（認証が必要な場合）
    if (strlen(client->username) > 0) {
        TurnAttributeHeader* username_attr = (TurnAttributeHeader*)(attributes + attr_offset);
        username_attr->type = htons(TURN_ATTR_USERNAME);
        int username_len = strlen(client->username);
        username_attr->length = htons(username_len);
        attr_offset += sizeof(TurnAttributeHeader);
        
        memcpy(attributes + attr_offset, client->username, username_len);
        attr_offset += username_len;
        // パディング（4バイト境界に合わせる）
        while (attr_offset % 4 != 0) {
            attributes[attr_offset++] = 0;
        }
    }
    
    // アロケーション要求の送信
    client->state = TURN_STATE_ALLOCATING;
    if (send_turn_message(client, TURN_ALLOCATION_REQUEST, attributes, attr_offset) < 0) {
        client->state = TURN_STATE_IDLE;
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // 応答の受信
    uint8_t response[TURN_MAX_BUFFER];
    uint16_t response_type, response_length;
    if (receive_turn_message(client, response, sizeof(response), 
                            &response_type, &response_length) < 0) {
        client->state = TURN_STATE_IDLE;
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // 応答の解析
    if (response_type == TURN_ALLOCATION_RESPONSE) {
        // アロケーション成功
        client->state = TURN_STATE_ALLOCATED;
        client->allocation_expiry = time(NULL) + TURN_ALLOCATION_LIFETIME;
        
        // リレーアドレスの取得
        int offset = sizeof(TurnMessageHeader);
        while (offset < sizeof(TurnMessageHeader) + response_length) {
            TurnAttributeHeader* attr = (TurnAttributeHeader*)(response + offset);
            uint16_t attr_type = ntohs(attr->type);
            uint16_t attr_length = ntohs(attr->length);
            
            if (attr_type == TURN_ATTR_XOR_RELAYED_ADDRESS) {
                // リレーアドレスの解析（XOR処理が必要）
                uint8_t family = response[offset + sizeof(TurnAttributeHeader) + 1];
                uint16_t port = ntohs(*(uint16_t*)(response + offset + sizeof(TurnAttributeHeader) + 2));
                uint32_t ip = ntohl(*(uint32_t*)(response + offset + sizeof(TurnAttributeHeader) + 4));
                
                // XOR処理（STUNの仕様に基づく）
                port ^= (0x2112A442 >> 16);
                ip ^= 0x2112A442;
                
                struct in_addr addr;
                addr.s_addr = htonl(ip);
                strncpy(client->relayed_ip, inet_ntoa(addr), MAX_IP_STR_LEN - 1);
                client->relayed_port = port;
                
                printf("TURN allocation successful for node %d. Relayed address: %s:%d\n", 
                       node->id, client->relayed_ip, client->relayed_port);
                
                // リフレッシュスレッドの開始
                client->refresh_running = true;
                if (pthread_create(&client->refresh_thread, NULL, turn_refresh_thread, node) != 0) {
                    perror("Failed to create TURN refresh thread");
                    client->refresh_running = false;
                }
                
                pthread_mutex_unlock(&client->mutex);
                return 0;
            }
            
            // 次の属性へ
            offset += sizeof(TurnAttributeHeader) + attr_length;
            // パディング（4バイト境界に合わせる）
            while (offset % 4 != 0) {
                offset++;
            }
        }
        
        // リレーアドレスが見つからなかった
        client->state = TURN_STATE_FAILED;
        pthread_mutex_unlock(&client->mutex);
        return -1;
    } else if (response_type == TURN_ALLOCATION_ERROR_RESPONSE) {
        // エラーコードの取得
        int error_code = 0;
        int offset = sizeof(TurnMessageHeader);
        while (offset < sizeof(TurnMessageHeader) + response_length) {
            TurnAttributeHeader* attr = (TurnAttributeHeader*)(response + offset);
            uint16_t attr_type = ntohs(attr->type);
            uint16_t attr_length = ntohs(attr->length);
            
            if (attr_type == TURN_ATTR_ERROR_CODE) {
                error_code = (response[offset + sizeof(TurnAttributeHeader) + 2] * 100) + 
                             response[offset + sizeof(TurnAttributeHeader) + 3];
                break;
            }
            
            // 次の属性へ
            offset += sizeof(TurnAttributeHeader) + attr_length;
            // パディング（4バイト境界に合わせる）
            while (offset % 4 != 0) {
                offset++;
            }
        }
        
        printf("TURN allocation failed for node %d with error code %d\n", 
               node->id, error_code);
        
        // 認証が必要な場合（401: Unauthorized）
        if (error_code == 401) {
            // Realm, Nonceの取得
            offset = sizeof(TurnMessageHeader);
            while (offset < sizeof(TurnMessageHeader) + response_length) {
                TurnAttributeHeader* attr = (TurnAttributeHeader*)(response + offset);
                uint16_t attr_type = ntohs(attr->type);
                uint16_t attr_length = ntohs(attr->length);
                
                if (attr_type == TURN_ATTR_REALM) {
                    memcpy(client->realm, response + offset + sizeof(TurnAttributeHeader), 
                           attr_length < sizeof(client->realm) ? attr_length : sizeof(client->realm) - 1);
                    client->realm[attr_length < sizeof(client->realm) ? attr_length : sizeof(client->realm) - 1] = '\0';
                } else if (attr_type == TURN_ATTR_NONCE) {
                    memcpy(client->nonce, response + offset + sizeof(TurnAttributeHeader), 
                           attr_length < sizeof(client->nonce) ? attr_length : sizeof(client->nonce) - 1);
                    client->nonce[attr_length < sizeof(client->nonce) ? attr_length : sizeof(client->nonce) - 1] = '\0';
                }
                
                // 次の属性へ
                offset += sizeof(TurnAttributeHeader) + attr_length;
                // パディング（4バイト境界に合わせる）
                while (offset % 4 != 0) {
                    offset++;
                }
            }
            
            // 認証情報を含めて再度アロケーション要求
            // （実際の実装では、ここでHMAC-SHA1を使用したMessage Integrityの計算が必要）
            // 簡略化のため、この部分は省略
        }
        
        client->state = TURN_STATE_FAILED;
        pthread_mutex_unlock(&client->mutex);
        return -1;
    } else {
        // 予期しない応答
        client->state = TURN_STATE_FAILED;
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
}

// TURNアロケーションのリフレッシュ
int turn_refresh(Node* node, int lifetime) {
    if (!node || !node->turn_data) {
        return -1;
    }
    
    TurnData* turn_data = (TurnData*)node->turn_data;
    TurnClient* client = &turn_data->client;
    
    pthread_mutex_lock(&client->mutex);
    
    // アロケーションされていない場合
    if (client->state != TURN_STATE_ALLOCATED) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // リフレッシュ要求の属性
    uint8_t attributes[256];
    int attr_offset = 0;
    
    // Lifetime属性
    TurnAttributeHeader* lifetime_attr = (TurnAttributeHeader*)(attributes + attr_offset);
    lifetime_attr->type = htons(TURN_ATTR_LIFETIME);
    lifetime_attr->length = htons(4);
    attr_offset += sizeof(TurnAttributeHeader);
    
    // Lifetime値（秒単位）
    *(uint32_t*)(attributes + attr_offset) = htonl(lifetime);
    attr_offset += 4;
    
    // リフレッシュ要求の送信
    if (send_turn_message(client, TURN_REFRESH_REQUEST, attributes, attr_offset) < 0) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // 応答の受信
    uint8_t response[TURN_MAX_BUFFER];
    uint16_t response_type, response_length;
    if (receive_turn_message(client, response, sizeof(response), 
                            &response_type, &response_length) < 0) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // 応答の解析
    if (response_type == TURN_REFRESH_RESPONSE) {
        // リフレッシュ成功
        client->allocation_expiry = time(NULL) + lifetime;
        
        printf("TURN refresh successful for node %d. New expiry: %ld\n", 
               node->id, client->allocation_expiry);
        
        pthread_mutex_unlock(&client->mutex);
        return 0;
    } else {
        // リフレッシュ失敗
        printf("TURN refresh failed for node %d\n", node->id);
        
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
}

// TURNリフレッシュスレッド
void* turn_refresh_thread(void* arg) {
    Node* node = (Node*)arg;
    TurnData* turn_data = (TurnData*)node->turn_data;
    TurnClient* client = &turn_data->client;
    
    while (client->refresh_running) {
        // アロケーション期限の80%経過時にリフレッシュ
        time_t now = time(NULL);
        time_t refresh_time = client->allocation_expiry - (TURN_ALLOCATION_LIFETIME * 0.2);
        
        if (now >= refresh_time) {
            turn_refresh(node, TURN_ALLOCATION_LIFETIME);
        }
        
        // 10秒ごとにチェック
        sleep(10);
    }
    
    return NULL;
}

// TURNパーミッションの作成
int turn_create_permission(Node* node, const char* peer_ip) {
    if (!node || !node->turn_data || !peer_ip) {
        return -1;
    }
    
    TurnData* turn_data = (TurnData*)node->turn_data;
    TurnClient* client = &turn_data->client;
    
    pthread_mutex_lock(&client->mutex);
    
    // アロケーションされていない場合
    if (client->state != TURN_STATE_ALLOCATED) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // パーミッション要求の属性
    uint8_t attributes[256];
    int attr_offset = 0;
    
    // XOR-Peer-Address属性
    TurnAttributeHeader* peer_addr_attr = (TurnAttributeHeader*)(attributes + attr_offset);
    peer_addr_attr->type = htons(TURN_ATTR_XOR_PEER_ADDRESS);
    peer_addr_attr->length = htons(8);
    attr_offset += sizeof(TurnAttributeHeader);
    
    // アドレスファミリー（IPv4）
    attributes[attr_offset++] = 0;
    attributes[attr_offset++] = 1;  // IPv4
    
    // ポート（XOR処理）
    uint16_t port = 0;  // ポートは0でよい（IPアドレスのみのパーミッション）
    *(uint16_t*)(attributes + attr_offset) = htons(port ^ (0x2112A442 >> 16));
    attr_offset += 2;
    
    // IPアドレス（XOR処理）
    struct in_addr addr;
    inet_aton(peer_ip, &addr);
    *(uint32_t*)(attributes + attr_offset) = addr.s_addr ^ htonl(0x2112A442);
    attr_offset += 4;
    
    // パーミッション要求の送信
    if (send_turn_message(client, TURN_CREATE_PERMISSION_REQUEST, attributes, attr_offset) < 0) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // 応答の受信
    uint8_t response[TURN_MAX_BUFFER];
    uint16_t response_type, response_length;
    if (receive_turn_message(client, response, sizeof(response), 
                            &response_type, &response_length) < 0) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // 応答の解析
    if (response_type == TURN_CREATE_PERMISSION_RESPONSE) {
        // パーミッション作成成功
        printf("TURN permission created for node %d to peer %s\n", 
               node->id, peer_ip);
        
        pthread_mutex_unlock(&client->mutex);
        return 0;
    } else {
        // パーミッション作成失敗
        printf("TURN permission creation failed for node %d to peer %s\n", 
               node->id, peer_ip);
        
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
}

// TURNを使用したデータ送信
int turn_send_data(Node* node, const char* peer_ip, int peer_port, const void* data, int data_len) {
    if (!node || !node->turn_data || !peer_ip || !data || data_len <= 0) {
        return -1;
    }
    
    TurnData* turn_data = (TurnData*)node->turn_data;
    TurnClient* client = &turn_data->client;
    
    pthread_mutex_lock(&client->mutex);
    
    // アロケーションされていない場合
    if (client->state != TURN_STATE_ALLOCATED) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    // Send Indicationの属性
    uint8_t attributes[TURN_MAX_BUFFER];
    int attr_offset = 0;
    
    // XOR-Peer-Address属性
    TurnAttributeHeader* peer_addr_attr = (TurnAttributeHeader*)(attributes + attr_offset);
    peer_addr_attr->type = htons(TURN_ATTR_XOR_PEER_ADDRESS);
    peer_addr_attr->length = htons(8);
    attr_offset += sizeof(TurnAttributeHeader);
    
    // アドレスファミリー（IPv4）
    attributes[attr_offset++] = 0;
    attributes[attr_offset++] = 1;  // IPv4
    
    // ポート（XOR処理）
    *(uint16_t*)(attributes + attr_offset) = htons(peer_port ^ (0x2112A442 >> 16));
    attr_offset += 2;
    
    // IPアドレス（XOR処理）
    struct in_addr addr;
    inet_aton(peer_ip, &addr);
    *(uint32_t*)(attributes + attr_offset) = addr.s_addr ^ htonl(0x2112A442);
    attr_offset += 4;
    
    // Data属性
    TurnAttributeHeader* data_attr = (TurnAttributeHeader*)(attributes + attr_offset);
    data_attr->type = htons(TURN_ATTR_DATA);
    data_attr->length = htons(data_len);
    attr_offset += sizeof(TurnAttributeHeader);
    
    // データのコピー
    memcpy(attributes + attr_offset, data, data_len);
    attr_offset += data_len;
    // パディング（4バイト境界に合わせる）
    while (attr_offset % 4 != 0) {
        attributes[attr_offset++] = 0;
    }
    
    // Send Indicationの送信
    if (send_turn_message(client, TURN_SEND_INDICATION, attributes, attr_offset) < 0) {
        pthread_mutex_unlock(&client->mutex);
        return -1;
    }
    
    printf("TURN data sent from node %d to %s:%d (%d bytes)\n", 
           node->id, peer_ip, peer_port, data_len);
    
    pthread_mutex_unlock(&client->mutex);
    return 0;
}

// TURNからのデータ処理
int turn_process_data(Node* node, const void* data, int data_len, char* from_ip, int* from_port) {
    if (!node || !node->turn_data || !data || data_len <= 0 || !from_ip || !from_port) {
        return -1;
    }
    
    // TURNメッセージの解析
    const uint8_t* buffer = (const uint8_t*)data;
    
    // ヘッダの確認
    if (data_len < sizeof(TurnMessageHeader)) {
        return -1;
    }
    
    const TurnMessageHeader* header = (const TurnMessageHeader*)buffer;
    uint16_t message_type = ntohs(header->message_type);
    uint16_t message_length = ntohs(header->message_length);
    
    // Data Indicationの場合
    if (message_type == TURN_DATA_INDICATION) {
        // 属性の解析
        int offset = sizeof(TurnMessageHeader);
        const void* payload = NULL;
        int payload_len = 0;
        
        while (offset < sizeof(TurnMessageHeader) + message_length) {
            const TurnAttributeHeader* attr = (const TurnAttributeHeader*)(buffer + offset);
            uint16_t attr_type = ntohs(attr->type);
            uint16_t attr_length = ntohs(attr->length);
            
            if (attr_type == TURN_ATTR_XOR_PEER_ADDRESS) {
                // 送信元アドレスの解析
                uint8_t family = buffer[offset + sizeof(TurnAttributeHeader) + 1];
                uint16_t port = ntohs(*(uint16_t*)(buffer + offset + sizeof(TurnAttributeHeader) + 2));
                uint32_t ip = ntohl(*(uint32_t*)(buffer + offset + sizeof(TurnAttributeHeader) + 4));
                
                // XOR処理（STUNの仕様に基づく）
                port ^= (0x2112A442 >> 16);
                ip ^= 0x2112A442;
                
                struct in_addr addr;
                addr.s_addr = htonl(ip);
                strncpy(from_ip, inet_ntoa(addr), MAX_IP_STR_LEN - 1);
                *from_port = port;
            } else if (attr_type == TURN_ATTR_DATA) {
                // データペイロード
                payload = buffer + offset + sizeof(TurnAttributeHeader);
                payload_len = attr_length;
            }
            
            // 次の属性へ
            offset += sizeof(TurnAttributeHeader) + attr_length;
            // パディング（4バイト境界に合わせる）
            while (offset % 4 != 0) {
                offset++;
            }
        }
        
        // ペイロードが見つかった場合
        if (payload && payload_len > 0) {
            printf("TURN data received by node %d from %s:%d (%d bytes)\n", 
                   node->id, from_ip, *from_port, payload_len);
            
            // ペイロードを返す
            return payload_len;
        }
    }
    
    return -1;
}