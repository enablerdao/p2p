#include "ice.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

// ICEの初期化
int ice_init(Node* node) {
    if (!node) {
        return -1;
    }
    
    // ICEデータの確保
    IceData* ice_data = (IceData*)malloc(sizeof(IceData));
    if (!ice_data) {
        perror("Failed to allocate ICE data");
        return -1;
    }
    
    // 初期化
    memset(ice_data, 0, sizeof(IceData));
    ice_data->session.state = ICE_STATE_NEW;
    
    // 制御側かどうかをランダムに決定
    srand(time(NULL) ^ node->id);
    ice_data->session.controlling = (rand() % 2 == 0);
    
    // タイブレーカー値の生成
    ice_data->session.tie_breaker = ((uint64_t)rand() << 32) | rand();
    
    // ミューテックスの初期化
    pthread_mutex_init(&ice_data->session.mutex, NULL);
    
    // ノードにICEデータを関連付ける
    node->ice_data = ice_data;
    
    printf("ICE initialized for node %d (controlling: %s)\n", 
           node->id, ice_data->session.controlling ? "true" : "false");
    
    return 0;
}

// ICEのクリーンアップ
int ice_cleanup(Node* node) {
    if (!node || !node->ice_data) {
        return -1;
    }
    
    IceData* ice_data = (IceData*)node->ice_data;
    
    // ICEスレッドの停止
    if (ice_data->session.ice_running) {
        ice_data->session.ice_running = false;
        pthread_join(ice_data->session.ice_thread, NULL);
    }
    
    // ミューテックスの破棄
    pthread_mutex_destroy(&ice_data->session.mutex);
    
    // メモリの解放
    free(ice_data);
    node->ice_data = NULL;
    
    printf("ICE cleaned up for node %d\n", node->id);
    
    return 0;
}

// マクロ定義
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

// 候補の優先度を計算
static int calculate_priority(IceCandidateType type, const char* ip) {
    // タイプ優先度
    int type_preference;
    switch (type) {
        case ICE_CANDIDATE_HOST:
            type_preference = 126;
            break;
        case ICE_CANDIDATE_SRFLX:
            type_preference = 100;
            break;
        case ICE_CANDIDATE_RELAY:
            type_preference = 0;
            break;
        default:
            type_preference = 0;
    }
    
    // ローカル優先度（単純化のため、常に1）
    int local_preference = 1;
    
    // コンポーネントID（単純化のため、常に1）
    int component_id = 1;
    
    // 優先度の計算（RFC 5245に基づく）
    return (type_preference << 24) | (local_preference << 8) | (256 - component_id);
}

// ICE候補の収集
int ice_gather_candidates(Node* node) {
    if (!node || !node->ice_data) {
        return -1;
    }
    
    IceData* ice_data = (IceData*)node->ice_data;
    
    pthread_mutex_lock(&ice_data->session.mutex);
    
    // 候補リストのクリア
    ice_data->session.local_candidate_count = 0;
    
    // ホスト候補の追加
    if (ice_data->session.local_candidate_count < 10) {
        IceCandidate* candidate = &ice_data->session.local_candidates[ice_data->session.local_candidate_count];
        candidate->type = ICE_CANDIDATE_HOST;
        strncpy(candidate->ip, node->ip, MAX_IP_STR_LEN - 1);
        candidate->port = ntohs(node->addr.sin_port);
        candidate->priority = calculate_priority(ICE_CANDIDATE_HOST, candidate->ip);
        candidate->nominated = false;
        
        printf("ICE gathered host candidate for node %d: %s:%d (priority: %d)\n", 
               node->id, candidate->ip, candidate->port, candidate->priority);
        
        ice_data->session.local_candidate_count++;
    }
    
    // Server Reflexive候補の追加（STUNを使用）
    if (node->is_behind_nat && ice_data->session.local_candidate_count < 10) {
        IceCandidate* candidate = &ice_data->session.local_candidates[ice_data->session.local_candidate_count];
        candidate->type = ICE_CANDIDATE_SRFLX;
        strncpy(candidate->ip, node->public_ip, MAX_IP_STR_LEN - 1);
        candidate->port = node->public_port;
        candidate->priority = calculate_priority(ICE_CANDIDATE_SRFLX, candidate->ip);
        candidate->nominated = false;
        
        printf("ICE gathered server reflexive candidate for node %d: %s:%d (priority: %d)\n", 
               node->id, candidate->ip, candidate->port, candidate->priority);
        
        ice_data->session.local_candidate_count++;
    }
    
    // Relay候補の追加（TURNを使用、もし利用可能なら）
    if (node->turn_data) {
        TurnData* turn_data = (TurnData*)node->turn_data;
        TurnClient* client = &turn_data->client;
        
        if (client->state == TURN_STATE_ALLOCATED && ice_data->session.local_candidate_count < 10) {
            IceCandidate* candidate = &ice_data->session.local_candidates[ice_data->session.local_candidate_count];
            candidate->type = ICE_CANDIDATE_RELAY;
            strncpy(candidate->ip, client->relayed_ip, MAX_IP_STR_LEN - 1);
            candidate->port = client->relayed_port;
            candidate->priority = calculate_priority(ICE_CANDIDATE_RELAY, candidate->ip);
            candidate->nominated = false;
            
            printf("ICE gathered relay candidate for node %d: %s:%d (priority: %d)\n", 
                   node->id, candidate->ip, candidate->port, candidate->priority);
            
            ice_data->session.local_candidate_count++;
        }
    }
    
    pthread_mutex_unlock(&ice_data->session.mutex);
    
    return ice_data->session.local_candidate_count;
}

// リモート候補の追加
int ice_add_remote_candidate(Node* node, IceCandidateType type, const char* ip, int port, int priority) {
    if (!node || !node->ice_data || !ip) {
        return -1;
    }
    
    IceData* ice_data = (IceData*)node->ice_data;
    
    pthread_mutex_lock(&ice_data->session.mutex);
    
    // 候補リストがいっぱいかどうか
    if (ice_data->session.remote_candidate_count >= 10) {
        pthread_mutex_unlock(&ice_data->session.mutex);
        return -1;
    }
    
    // 新しい候補の追加
    IceCandidate* candidate = &ice_data->session.remote_candidates[ice_data->session.remote_candidate_count];
    candidate->type = type;
    strncpy(candidate->ip, ip, MAX_IP_STR_LEN - 1);
    candidate->port = port;
    candidate->priority = priority;
    candidate->nominated = false;
    
    printf("ICE added remote candidate for node %d: %s:%d (type: %d, priority: %d)\n", 
           node->id, ip, port, type, priority);
    
    ice_data->session.remote_candidate_count++;
    
    pthread_mutex_unlock(&ice_data->session.mutex);
    
    return 0;
}

// 接続性チェックの開始
int ice_start_connectivity_checks(Node* node) {
    if (!node || !node->ice_data) {
        return -1;
    }
    
    IceData* ice_data = (IceData*)node->ice_data;
    
    pthread_mutex_lock(&ice_data->session.mutex);
    
    // 状態の更新
    ice_data->session.state = ICE_STATE_CHECKING;
    
    // ICEスレッドの開始
    ice_data->session.ice_running = true;
    if (pthread_create(&ice_data->session.ice_thread, NULL, ice_thread, node) != 0) {
        perror("Failed to create ICE thread");
        ice_data->session.ice_running = false;
        ice_data->session.state = ICE_STATE_FAILED;
        pthread_mutex_unlock(&ice_data->session.mutex);
        return -1;
    }
    
    printf("ICE connectivity checks started for node %d\n", node->id);
    
    pthread_mutex_unlock(&ice_data->session.mutex);
    
    return 0;
}

// 接続状態の取得
IceConnectionState ice_get_connection_state(Node* node) {
    if (!node || !node->ice_data) {
        return ICE_STATE_FAILED;
    }
    
    IceData* ice_data = (IceData*)node->ice_data;
    
    pthread_mutex_lock(&ice_data->session.mutex);
    IceConnectionState state = ice_data->session.state;
    pthread_mutex_unlock(&ice_data->session.mutex);
    
    return state;
}

// ICEを使用したデータ送信
int ice_send_data(Node* node, const void* data, int data_len) {
    if (!node || !node->ice_data || !data || data_len <= 0) {
        return -1;
    }
    
    IceData* ice_data = (IceData*)node->ice_data;
    
    pthread_mutex_lock(&ice_data->session.mutex);
    
    // 接続が確立されているか確認
    if (ice_data->session.state != ICE_STATE_CONNECTED && 
        ice_data->session.state != ICE_STATE_COMPLETED) {
        pthread_mutex_unlock(&ice_data->session.mutex);
        return -1;
    }
    
    // 選択された候補ペアを使用してデータを送信
    IceCandidate* remote = &ice_data->session.selected_pair[1];
    
    // 候補タイプに応じた送信方法の選択
    int result = -1;
    
    if (ice_data->session.selected_pair[0].type == ICE_CANDIDATE_RELAY) {
        // Relay候補の場合はTURNを使用
        result = turn_send_data(node, remote->ip, remote->port, data, data_len);
    } else {
        // それ以外の場合は直接送信
        struct sockaddr_in to_addr;
        memset(&to_addr, 0, sizeof(to_addr));
        to_addr.sin_family = AF_INET;
        to_addr.sin_addr.s_addr = inet_addr(remote->ip);
        to_addr.sin_port = htons(remote->port);
        
        result = sendto(node->socket_fd, data, data_len, 0, 
                       (struct sockaddr*)&to_addr, sizeof(to_addr));
    }
    
    if (result >= 0) {
        printf("ICE sent data from node %d to %s:%d (%d bytes)\n", 
               node->id, remote->ip, remote->port, data_len);
    } else {
        printf("ICE failed to send data from node %d to %s:%d\n", 
               node->id, remote->ip, remote->port);
    }
    
    pthread_mutex_unlock(&ice_data->session.mutex);
    
    return result;
}

// 候補ペアの優先度を計算
static uint64_t calculate_pair_priority(IceCandidate* local, IceCandidate* remote, bool controlling) {
    // RFC 5245に基づく計算
    uint64_t g = controlling ? local->priority : remote->priority;
    uint64_t d = controlling ? remote->priority : local->priority;
    
    return (1ULL << 32) * MIN(g, d) + 2 * MAX(g, d) + (g > d ? 1 : 0);
}

// 最適な候補ペアの選択
static void select_best_candidate_pair(IceSession* session) {
    uint64_t best_priority = 0;
    int best_local_index = -1;
    int best_remote_index = -1;
    
    // すべての候補ペアを評価
    for (int i = 0; i < session->local_candidate_count; i++) {
        for (int j = 0; j < session->remote_candidate_count; j++) {
            uint64_t priority = calculate_pair_priority(
                &session->local_candidates[i], 
                &session->remote_candidates[j], 
                session->controlling);
            
            if (priority > best_priority) {
                best_priority = priority;
                best_local_index = i;
                best_remote_index = j;
            }
        }
    }
    
    // 最適なペアが見つかった場合
    if (best_local_index >= 0 && best_remote_index >= 0) {
        session->selected_pair[0] = session->local_candidates[best_local_index];
        session->selected_pair[1] = session->remote_candidates[best_remote_index];
        session->selected_pair[0].nominated = true;
        session->selected_pair[1].nominated = true;
        
        // 状態の更新
        session->state = ICE_STATE_CONNECTED;
    } else {
        // 候補ペアが見つからない場合
        session->state = ICE_STATE_FAILED;
    }
}

// ICEスレッド
void* ice_thread(void* arg) {
    Node* node = (Node*)arg;
    IceData* ice_data = (IceData*)node->ice_data;
    
    // 接続性チェックの実行
    // （実際の実装では、STUNバインディングリクエストを使用した
    //   接続性チェックが必要ですが、簡略化のため省略）
    
    pthread_mutex_lock(&ice_data->session.mutex);
    
    // 最適な候補ペアの選択
    select_best_candidate_pair(&ice_data->session);
    
    if (ice_data->session.state == ICE_STATE_CONNECTED) {
        printf("ICE connection established for node %d using %s:%d -> %s:%d\n", 
               node->id, 
               ice_data->session.selected_pair[0].ip, 
               ice_data->session.selected_pair[0].port, 
               ice_data->session.selected_pair[1].ip, 
               ice_data->session.selected_pair[1].port);
    } else {
        printf("ICE connection failed for node %d\n", node->id);
    }
    
    pthread_mutex_unlock(&ice_data->session.mutex);
    
    // 定期的な接続性チェック（キープアライブ）
    while (ice_data->session.ice_running) {
        // 10秒ごとにチェック
        sleep(10);
        
        pthread_mutex_lock(&ice_data->session.mutex);
        
        // 接続が確立されている場合のみ
        if (ice_data->session.state == ICE_STATE_CONNECTED || 
            ice_data->session.state == ICE_STATE_COMPLETED) {
            // キープアライブの送信
            // （実際の実装では、STUNバインディングリクエストを送信）
        }
        
        pthread_mutex_unlock(&ice_data->session.mutex);
    }
    
    return NULL;
}