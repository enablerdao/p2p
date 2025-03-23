#ifndef ICE_H
#define ICE_H

#include "node.h"
#include "stun.h"
#include "turn.h"

// ICE候補タイプ
typedef enum {
    ICE_CANDIDATE_HOST,       // ホスト候補（ローカルアドレス）
    ICE_CANDIDATE_SRFLX,      // Server Reflexive候補（STUNで取得したアドレス）
    ICE_CANDIDATE_RELAY       // Relay候補（TURNで取得したアドレス）
} IceCandidateType;

// ICE候補
typedef struct {
    IceCandidateType type;
    char ip[MAX_IP_STR_LEN];
    int port;
    int priority;             // 優先度
    bool nominated;           // 選択された候補かどうか
} IceCandidate;

// ICE接続状態
typedef enum {
    ICE_STATE_NEW,
    ICE_STATE_CHECKING,
    ICE_STATE_CONNECTED,
    ICE_STATE_COMPLETED,
    ICE_STATE_FAILED,
    ICE_STATE_DISCONNECTED,
    ICE_STATE_CLOSED
} IceConnectionState;

// ICEセッション
typedef struct {
    IceCandidate local_candidates[10];
    int local_candidate_count;
    IceCandidate remote_candidates[10];
    int remote_candidate_count;
    IceCandidate selected_pair[2];  // [0]: ローカル候補, [1]: リモート候補
    IceConnectionState state;
    bool controlling;         // 制御側かどうか
    uint64_t tie_breaker;     // タイブレーカー値
    pthread_t ice_thread;
    bool ice_running;
    pthread_mutex_t mutex;
} IceSession;

// ICEデータ
typedef struct {
    IceSession session;
} IceData;

// 関数プロトタイプ
int ice_init(Node* node);
int ice_cleanup(Node* node);
int ice_gather_candidates(Node* node);
int ice_add_remote_candidate(Node* node, IceCandidateType type, const char* ip, int port, int priority);
int ice_start_connectivity_checks(Node* node);
IceConnectionState ice_get_connection_state(Node* node);
int ice_send_data(Node* node, const void* data, int data_len);
void* ice_thread(void* arg);

#endif /* ICE_H */