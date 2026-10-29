#include "t7o_mac_cooppause.h"
#include "t7o_cooppause.h"
#include "t7o_mac_offsets.h"
#include "t7o_mac_pause.h"
#include "prepatch.h"
#include "dobby.h"
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static T7oCoopPause state;
static int (*original_server_frame)(int);
static void (*original_client_frame)(int,int,float),(*original_tokenize)(const char *);
static bool (*original_check)(void);
static void *(*find_dvar)(const char *),*(*thread_context)(void);
static const char *(*dvar_text)(const void *);
static void (*set_string)(const char *,const char *,bool);
static void (*send_command)(void *,int,const char *);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static void *pointer(const void *p) { void *v;memcpy(&v,p,sizeof(v));return v; }
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
#ifndef T7O_COOP_MILLISECONDS
static uint64_t milliseconds(void) {
    struct timespec time;if (clock_gettime(CLOCK_MONOTONIC,&time)) return 0;
    return (uint64_t)time.tv_sec*1000+(uint64_t)time.tv_nsec/1000000;
}
#define T7O_COOP_MILLISECONDS milliseconds
#endif
static int integer(const void *variable,int fallback) {
    const char *text=variable ? dvar_text(variable) : NULL;if (!text || !*text) return fallback;
    char *end;long value=strtol(text,&end,10);
    return *end || value<INT_MIN || value>INT_MAX ? fallback : (int)value;
}
typedef struct { uint8_t *clients;int count; } Transport;
static bool send(void *user,unsigned index,const char *text,uint32_t *sequence) {
    Transport *transport=user;
    if (!transport->clients || transport->clients!=pointer(address(T7O_MAC_SV_CLIENTS)) || index>=(unsigned)transport->count) return false;
    uint8_t *client=transport->clients+(size_t)index*T7O_MAC_SV_CLIENT_STRIDE;
    send_command(client,1,text);*sequence=read32(client+0x5030);return true;
}
static void set(void *user,const char *name,const char *value) { (void)user;set_string(name,value,true); }
static T7oCoopContext snapshot(Transport *transport) {
    void *map=find_dvar("mapname");
    T7oCoopContext c={.in_game=t7o_coop_pause_map_active(map ? dvar_text(map) : NULL),.now=T7O_COOP_MILLISECONDS(),
        .server_role=integer(pointer(address(T7O_MAC_SV_RUNNING_DVAR)),-1),
        .paused=integer(pointer(address(T7O_MAC_CL_PAUSED_DVAR)),-1),
        .max_clients=integer(pointer(address(T7O_MAC_SV_MAXCLIENTS_DVAR)),-1),
        .remote_enabled=integer(find_dvar("aae_coop_pause_remote"),0)>0,
        .total=-1,.remote_connected=-1,.remote_active=-1,.argument_state=-1,.user=transport,.send=send,.set=set};
    c.local_menu_pause=t7o_mac_pause_original_client();
    transport->clients=pointer(address(T7O_MAC_SV_CLIENTS));transport->count=c.max_clients;
    if (transport->clients && c.max_clients>=1 && c.max_clients<=T7O_COOP_CLIENTS) {
        c.total=c.remote_connected=c.remote_active=0;
        for (unsigned i=0;i<(unsigned)c.max_clients;++i) {
            const uint8_t *p=transport->clients+(size_t)i*T7O_MAC_SV_CLIENT_STRIDE;
            unsigned type=read32(p+0x34);T7oCoopClient *client=&c.clients[i];
            client->state=(int)read32(p);client->remote=(type==3 || type==4) && read32(p+0xbb460)==0;
            client->sequence=read32(p+0x5030);client->ack=read32(p+0x5034);
            if (client->state>2) {
                ++c.total;if (client->remote) { ++c.remote_connected;if (client->state==5) ++c.remote_active; }
                else if (client->state==5) c.local_active=true;
            }
        }
    }
    void *thread=thread_context(),*args=thread ? pointer((uint8_t *)thread+0x18) : NULL;
    if (args) { unsigned depth=read32(args);if (depth<4) c.argument_state=read32((uint8_t *)args+0x64+depth*4)>2; }
    return c;
}
static int server_frame(int msec) {
    if (!atomic_load(&active)) return original_server_frame(msec);
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);t7o_coop_pause_server_tick(&state,&c);
    bool frozen=t7o_coop_pause_freeze_host(&state,&c);pthread_mutex_unlock(&lock);
    return original_server_frame(frozen ? 0 : msec);
}
static void client_frame(int controller,int msec,float scale) {
    if (!atomic_load(&active)) { original_client_frame(controller,msec,scale);return; }
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);bool frozen=t7o_coop_pause_client_tick(&state,&c);pthread_mutex_unlock(&lock);
    original_client_frame(controller,frozen ? 0 : msec,frozen ? 0 : scale);
}
static bool check(void) {
    if (!atomic_load(&active)) return original_check();
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);bool allowed=t7o_coop_pause_allow_server(&state,&c);pthread_mutex_unlock(&lock);
    return allowed && original_check();
}
static void tokenize_for(const char *text,uintptr_t caller) {
    if (!atomic_load(&active) || caller!=base+T7O_MAC_SERVER_TOKENIZE_RETURN) { original_tokenize(text);return; }
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);const char *translated=t7o_coop_pause_receive(&state,&c,text);pthread_mutex_unlock(&lock);
    original_tokenize(translated);
}
static void tokenize(const char *text) { tokenize_for(text,(uintptr_t)__builtin_return_address(0)); }
bool t7o_mac_cooppause_force_menu(void) {
    if (!atomic_load(&active)) return false;
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);bool force=t7o_coop_pause_force_menu(&state,&c);pthread_mutex_unlock(&lock);return force;
}
void t7o_mac_cooppause_deactivate(void) {
    atomic_store(&active,false);pthread_mutex_lock(&lock);t7o_coop_pause_reset(&state);pthread_mutex_unlock(&lock);
}
bool t7o_mac_cooppause_activate(void) {
    if (!installed) return false;
    if (!find_dvar("aae_coop_pause_remote")) set_string("aae_coop_pause_remote","0",true);
    pthread_mutex_lock(&lock);t7o_coop_pause_reset(&state);pthread_mutex_unlock(&lock);
    atomic_store(&active,true);return true;
}
bool t7o_mac_cooppause_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint32_t sites[]={T7O_MAC_SERVER_FRAME,T7O_MAC_CLIENT_FRAME,T7O_MAC_CHECK_PAUSED,T7O_MAC_TOKENIZE};
    const uint8_t first[]={0x55,0x48,0x89,0xe5,0x53},second[]={0x55,0x48,0x89,0xe5,0x41,0x56};
    for (unsigned i=0;i<4;++i) {
        const uint8_t *bytes=i<2 ? first : second;size_t n=i<2 ? sizeof(first) : sizeof(second);
        if (memcmp(prepatch_original_code(address(sites[i]),n),bytes,n)) return false;
    }
    find_dvar=address(T7O_MAC_DVAR_FIND_NAME);dvar_text=address(T7O_MAC_DVAR_VALUE_STRING);set_string=address(T7O_MAC_SET_DVAR_STRING);
    thread_context=address(T7O_MAC_THREAD_CONTEXT);send_command=address(T7O_MAC_SEND_SERVER_COMMAND);
    if (DobbyHook(address(T7O_MAC_SERVER_FRAME),(void *)server_frame,(void **)&original_server_frame)!=0 ||
        DobbyHook(address(T7O_MAC_CLIENT_FRAME),(void *)client_frame,(void **)&original_client_frame)!=0 ||
        DobbyHook(address(T7O_MAC_CHECK_PAUSED),(void *)check,(void **)&original_check)!=0 ||
        DobbyHook(address(T7O_MAC_TOKENIZE),(void *)tokenize,(void **)&original_tokenize)!=0) return false;
    installed=true;return true;
}
