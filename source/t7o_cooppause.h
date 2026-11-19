#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { T7O_COOP_CLIENTS=18 };
typedef struct { int state;bool remote;uint32_t sequence,ack; } T7oCoopClient;
typedef struct {
    bool in_game,remote_enabled;
    int server_role,paused,total,remote_connected,remote_active,max_clients,argument_state;
    uint64_t now;
    T7oCoopClient clients[T7O_COOP_CLIENTS];
    void *user;
    bool (*send)(void *,unsigned,const char *,uint32_t *);
    void (*set)(void *,const char *,const char *);
} T7oCoopContext;
typedef struct {
    bool host_frozen,aborted,acknowledged,negotiating;
    uint64_t started_at;
    uint32_t participants,required[T7O_COOP_CLIENTS],last_ack[T7O_COOP_CLIENTS],nonce;
    uint64_t sent_at[T7O_COOP_CLIENTS],progress_at[T7O_COOP_CLIENTS],last_sent,heartbeat_at;
    bool remote_paused;
    uint64_t remote_since;
    int published;
} T7oCoopPause;
void t7o_coop_pause_reset(T7oCoopPause *);
bool t7o_coop_pause_map_active(const char *);
void t7o_coop_pause_server_tick(T7oCoopPause *,T7oCoopContext *);
bool t7o_coop_pause_allow_server(T7oCoopPause *,T7oCoopContext *);
bool t7o_coop_pause_client_tick(T7oCoopPause *,T7oCoopContext *);
bool t7o_coop_pause_freeze_host(const T7oCoopPause *,const T7oCoopContext *);
bool t7o_coop_pause_force_menu(T7oCoopPause *,const T7oCoopContext *);
const char *t7o_coop_pause_receive(T7oCoopPause *,T7oCoopContext *,const char *);
