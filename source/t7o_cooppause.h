#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { T7O_COOP_CLIENTS=18 };
typedef struct { int state;bool remote;uint32_t sequence,ack; } T7oCoopClient;
typedef struct {
    bool in_game,remote_enabled,local_menu_pause,local_active;
    int server_role,paused,total,remote_connected,remote_active,max_clients,argument_state;
    uint32_t session_flags;
    uint64_t now;
    T7oCoopClient clients[T7O_COOP_CLIENTS];
    void *user;
    bool (*send)(void *,unsigned,const char *,uint32_t *);
    void (*set)(void *,const char *,const char *);
} T7oCoopContext;
typedef struct {
    bool host_frozen,aborted,acknowledged,negotiating,was_multi,local_grace,local_ready;
    uint64_t acknowledged_at,started_at,local_since;
    uint32_t participants,required[T7O_COOP_CLIENTS],last_ack[T7O_COOP_CLIENTS],nonce;
    uint64_t sent_at[T7O_COOP_CLIENTS],progress_at[T7O_COOP_CLIENTS],last_sent,heartbeat_at;
    bool failure_reported,needs_release,had_multi,local_transition;
    bool client_frozen,client_role_known,client_is_guest,received,remote_paused,client_expired;
    uint64_t client_tick,remote_since;
    int published;
} T7oCoopPause;
void t7o_coop_pause_reset(T7oCoopPause *);
bool t7o_coop_pause_map_active(const char *);
void t7o_coop_pause_server_tick(T7oCoopPause *,T7oCoopContext *);
bool t7o_coop_pause_allow_server(T7oCoopPause *,T7oCoopContext *);
bool t7o_coop_pause_client_tick(T7oCoopPause *,T7oCoopContext *);
bool t7o_coop_pause_freeze_host(T7oCoopPause *,const T7oCoopContext *);
bool t7o_coop_pause_force_menu(T7oCoopPause *,const T7oCoopContext *);
const char *t7o_coop_pause_receive(T7oCoopPause *,T7oCoopContext *,const char *);
