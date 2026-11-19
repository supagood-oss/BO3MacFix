#include "t7o_mac_cooppause.h"
#include "t7o_cooppause.h"
#include "t7o_aae_pause_channel.h"
#include "t7o_mac_offsets.h"
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
static bool channel_installed;
enum { PAUSE_DISABLED, PAUSE_LEGACY, PAUSE_CHANNEL };
static atomic_int mode;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static T7oCoopPause state;
static T7oAaePauseChannel channel;
static char channel_map[65];
static bool channel_guest_paused;
static int (*original_server_frame)(int);
static void (*original_client_frame)(int,int,float),(*original_tokenize)(const char *);
static bool (*original_check)(void);
static void *(*find_dvar)(const char *),*(*thread_context)(void);
static const char *(*dvar_text)(const void *);
static void (*set_string)(const char *,const char *,bool);
static void (*send_command)(void *,int,const char *);
typedef struct {
    uint8_t flags[8];
    const uint8_t *data;
    uint8_t unused[12];
    int32_t size;
    uint8_t used[20];
    int32_t target_socket;
} AaeMacMessage;
_Static_assert(sizeof(AaeMacMessage)==0x38 && offsetof(AaeMacMessage,size)==0x1c &&
    offsetof(AaeMacMessage,target_socket)==0x34,"BO3 Mac message layout");
static int (*original_get_packet)(T7oAaePauseAddress *, AaeMacMessage *);
static bool (*send_packet)(int,int,const void *,T7oAaePauseAddress);
static int (*local_socket)(int);
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
#ifndef T7O_AAE_NONCE
static uint64_t channel_nonce(void) {
    uint64_t nonce;
    do { arc4random_buf(&nonce,sizeof(nonce)); } while (!nonce);
    return nonce;
}
#define T7O_AAE_NONCE channel_nonce
#endif
typedef struct { uint8_t *clients;int count;char map[65]; } Transport;
static bool send(void *user,unsigned index,const char *text,uint32_t *sequence) {
    Transport *transport=user;
    if (!transport->clients || transport->clients!=pointer(address(T7O_MAC_SV_CLIENTS)) || index>=(unsigned)transport->count) return false;
    uint8_t *client=transport->clients+(size_t)index*T7O_MAC_SV_CLIENT_STRIDE;
    send_command(client,1,text);*sequence=read32(client+0x5030);return true;
}
static void set(void *user,const char *name,const char *value) { (void)user;set_string(name,value,true); }
static T7oCoopContext snapshot(Transport *transport) {
    void *map=find_dvar("mapname");
    const char *name=map ? dvar_text(map) : NULL;
    bool in_game=t7o_coop_pause_map_active(name);
    transport->map[0]='\0';
    if (in_game) memcpy(transport->map,name,strlen(name)+1);
    T7oCoopContext c={.in_game=in_game,.now=T7O_COOP_MILLISECONDS(),
        .server_role=integer(pointer(address(T7O_MAC_SV_RUNNING_DVAR)),-1),
        .paused=integer(pointer(address(T7O_MAC_CL_PAUSED_DVAR)),-1),
        .max_clients=integer(pointer(address(T7O_MAC_SV_MAXCLIENTS_DVAR)),-1),
        .remote_enabled=integer(find_dvar("aae_coop_pause_remote"),0)>0,
        .total=-1,.remote_connected=-1,.remote_active=-1,.argument_state=-1,.user=transport,.send=send,.set=set};
    transport->clients=c.server_role==1 ? pointer(address(T7O_MAC_SV_CLIENTS)) : NULL;
    transport->count=c.max_clients;
    if (transport->clients && c.max_clients>=1 && c.max_clients<=T7O_COOP_CLIENTS) {
        c.total=c.remote_connected=c.remote_active=0;
        for (unsigned i=0;i<(unsigned)c.max_clients;++i) {
            const uint8_t *p=transport->clients+(size_t)i*T7O_MAC_SV_CLIENT_STRIDE;
            unsigned type=read32(p+0x34);T7oCoopClient *client=&c.clients[i];
            client->state=(int)read32(p);client->remote=(type==3 || type==4) && read32(p+0xbb460)==0;
            client->sequence=read32(p+0x5030);client->ack=read32(p+0x5034);
            if (client->state>2) {
                ++c.total;if (client->remote) { ++c.remote_connected;if (client->state==5) ++c.remote_active; }
            }
        }
    }
    void *thread=thread_context(),*args=thread ? pointer((uint8_t *)thread+0x18) : NULL;
    if (args) { unsigned depth=read32(args);if (depth<4) c.argument_state=read32((uint8_t *)args+0x64+depth*4)>2; }
    return c;
}
static void select_channel(const Transport *transport, T7oAaePauseRole role, int socket) {
    if (channel.role==role && channel.socket==socket && !strcmp(channel_map,transport->map)) return;
    t7o_aae_pause_begin(&channel,role,socket,role==T7O_AAE_PAUSE_IDLE ? 0 : T7O_AAE_NONCE());
    memcpy(channel_map,transport->map,strlen(transport->map)+1);
}
static bool channel_host_frame(T7oCoopContext *c,const Transport *transport) {
    T7oAaePauseAddress guests[T7O_AAE_PAUSE_PEERS]={0};
    for (unsigned i=0;i<T7O_AAE_PAUSE_PEERS && i<(unsigned)c->max_clients && transport->clients;++i)
        if (c->clients[i].state==5 && c->clients[i].remote)
            memcpy(&guests[i],transport->clients+i*(size_t)T7O_MAC_SV_CLIENT_STRIDE+
                T7O_MAC_AAE_GUEST_ADDRESS,sizeof(guests[i]));
    pthread_mutex_lock(&lock);
    if (atomic_load(&mode)!=PAUSE_CHANNEL) { pthread_mutex_unlock(&lock);return false; }
    if (!c->in_game || c->server_role!=1 || c->total<=0) {
        if (channel.role==T7O_AAE_PAUSE_HOST) select_channel(transport,T7O_AAE_PAUSE_IDLE,0);
        pthread_mutex_unlock(&lock);
        return false;
    }
    select_channel(transport,T7O_AAE_PAUSE_HOST,4);
    bool want=c->remote_enabled && c->paused==1 && c->remote_connected>0;
    bool release=!c->remote_enabled && channel.paused;
    release|=t7o_aae_pause_host_frame(&channel,c->now,want,guests,(unsigned)c->remote_connected);
    bool frozen=channel.frozen || (c->remote_connected==0 && c->paused==1);
    pthread_mutex_unlock(&lock);
    if (release) set(NULL,"cl_paused","0");
    return frozen && !release;
}
static bool channel_client_frame(T7oCoopContext *c,const Transport *transport) {
    T7oAaePauseAddress host={0};
    int socket=-1;
    if (c->in_game && c->server_role==0 && read32(address(T7O_MAC_AAE_CONNECTION_STATE))>=7) {
        const uint8_t *connection=pointer(address(T7O_MAC_CLIENT_CONNECTIONS));
        if (connection) {
            memcpy(&host,connection+T7O_MAC_AAE_HOST_ADDRESS,sizeof(host));
            socket=local_socket(0);
        }
    }
    pthread_mutex_lock(&lock);
    if (atomic_load(&mode)!=PAUSE_CHANNEL) { pthread_mutex_unlock(&lock);return false; }
    if (c->in_game && c->server_role==1) {
        bool frozen=(c->remote_connected==0 && c->total>0 && c->paused==1) ||
            (channel.role==T7O_AAE_PAUSE_HOST && channel.frozen && c->paused==1);
        bool leaving_guest=channel_guest_paused;
        channel_guest_paused=false;
        pthread_mutex_unlock(&lock);
        if (leaving_guest) set(NULL,"aae_coop_paused","0");
        return frozen;
    }
    bool usable=socket>=0 && socket<4 && t7o_aae_pause_address_valid(host);
    if (usable) select_channel(transport,T7O_AAE_PAUSE_GUEST,socket);
    else if (channel.role==T7O_AAE_PAUSE_GUEST) select_channel(transport,T7O_AAE_PAUSE_IDLE,0);
    bool frozen=usable && t7o_aae_pause_guest_frame(&channel,c->now,host);
    bool changed=frozen!=channel_guest_paused;
    uint64_t generation=channel.generation, nonce=channel.nonce;
    channel_guest_paused=frozen;
    pthread_mutex_unlock(&lock);
    if (changed) {
        if (c->in_game && c->server_role==0) set(NULL,"cl_paused",frozen ? "1" : "0");
        set(NULL,"aae_coop_paused",frozen ? "1" : "0");
    }
    // Acknowledge only after the frame thread has applied the new state.
    if (usable && generation) {
        pthread_mutex_lock(&lock);
        if (channel.role==T7O_AAE_PAUSE_GUEST && channel.nonce==nonce && channel.generation==generation)
            channel.applied=generation;
        pthread_mutex_unlock(&lock);
    }
    return frozen;
}
static int channel_get_packet(T7oAaePauseAddress *from,AaeMacMessage *message) {
    if (atomic_load(&mode)!=PAUSE_CHANNEL) return original_get_packet(from,message);
    uint64_t now=T7O_COOP_MILLISECONDS();
    T7oAaePauseSend outbound[T7O_AAE_PAUSE_PEERS];
    pthread_mutex_lock(&lock);
    int socket=channel.socket;
    size_t count=t7o_aae_pause_outbound(&channel,now,outbound);
    pthread_mutex_unlock(&lock);
    // Use the same engine transport, on the thread that pumps game packets.
    for (size_t i=0;i<count && atomic_load(&mode)==PAUSE_CHANNEL;++i)
        send_packet(socket,T7O_AAE_PAUSE_MESSAGE_SIZE,outbound[i].message,outbound[i].address);
    AaeMacMessage before=*message;
    for (unsigned i=0;i<32;++i) {
        int result=original_get_packet(from,message);
        if (!result || message->size<8) return result;
        pthread_mutex_lock(&lock);
        bool consumed=atomic_load(&mode)==PAUSE_CHANNEL && t7o_aae_pause_receive(&channel,now,message->target_socket,*from,
            message->data,(size_t)message->size,message->size==T7O_AAE_PAUSE_MESSAGE_SIZE ? T7O_AAE_NONCE() : 0);
        pthread_mutex_unlock(&lock);
        if (!consumed) return result;
        *message=before; // The engine advanced the data pointer past its routing header.
    }
    return 0;
}
static int server_frame(int msec) {
    int selected=atomic_load(&mode);
    if (selected==PAUSE_DISABLED) return original_server_frame(msec);
    Transport transport;T7oCoopContext c=snapshot(&transport);
    if (selected==PAUSE_CHANNEL) return original_server_frame(channel_host_frame(&c,&transport) ? 0 : msec);
    pthread_mutex_lock(&lock);t7o_coop_pause_server_tick(&state,&c);
    bool frozen=t7o_coop_pause_freeze_host(&state,&c);pthread_mutex_unlock(&lock);
    return original_server_frame(frozen ? 0 : msec);
}
static void client_frame(int controller,int msec,float scale) {
    int selected=atomic_load(&mode);
    if (selected==PAUSE_DISABLED) { original_client_frame(controller,msec,scale);return; }
    Transport transport;T7oCoopContext c=snapshot(&transport);
    bool frozen;
    if (selected==PAUSE_CHANNEL) frozen=channel_client_frame(&c,&transport);
    else { pthread_mutex_lock(&lock);frozen=t7o_coop_pause_client_tick(&state,&c);pthread_mutex_unlock(&lock); }
    original_client_frame(controller,frozen ? 0 : msec,frozen ? 0 : scale);
}
static bool check(void) {
    int selected=atomic_load(&mode);
    if (selected==PAUSE_DISABLED) return original_check();
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);
    bool allowed=selected==PAUSE_LEGACY ? t7o_coop_pause_allow_server(&state,&c) :
        !(c.in_game && ((c.server_role==1 && c.remote_connected>0 && c.remote_enabled && c.paused==1) ||
            (c.server_role==0 && channel_guest_paused)));
    pthread_mutex_unlock(&lock);
    return allowed && original_check();
}
static void tokenize_for(const char *text,uintptr_t caller) {
    if (atomic_load(&mode)!=PAUSE_LEGACY || caller!=base+T7O_MAC_SERVER_TOKENIZE_RETURN) { original_tokenize(text);return; }
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);const char *translated=t7o_coop_pause_receive(&state,&c,text);pthread_mutex_unlock(&lock);
    original_tokenize(translated);
}
static void tokenize(const char *text) { tokenize_for(text,(uintptr_t)__builtin_return_address(0)); }
bool t7o_mac_cooppause_force_menu(void) {
    int selected=atomic_load(&mode);
    if (selected==PAUSE_DISABLED) return false;
    Transport transport;T7oCoopContext c=snapshot(&transport);
    pthread_mutex_lock(&lock);
    bool force=selected==PAUSE_LEGACY ? t7o_coop_pause_force_menu(&state,&c) :
        c.in_game && ((c.server_role==0 && channel_guest_paused) ||
            (c.server_role==1 && c.total>0 && (c.remote_connected==0 || c.remote_enabled)));
    pthread_mutex_unlock(&lock);
    return force;
}
void t7o_mac_cooppause_deactivate(void) {
    atomic_store(&mode,PAUSE_DISABLED);
    pthread_mutex_lock(&lock);
    t7o_coop_pause_reset(&state);
    t7o_aae_pause_begin(&channel,T7O_AAE_PAUSE_IDLE,0,0);
    channel_map[0]='\0';
    channel_guest_paused=false;
    pthread_mutex_unlock(&lock);
}
bool t7o_mac_cooppause_activate(void) {
    if (!installed) return false;
    if (!find_dvar("aae_coop_pause_remote")) set_string("aae_coop_pause_remote","0",true);
    pthread_mutex_lock(&lock);t7o_coop_pause_reset(&state);pthread_mutex_unlock(&lock);
    atomic_store(&mode,PAUSE_LEGACY);return true;
}
bool t7o_mac_cooppause_activate_channel(void) {
    if (!installed || !channel_installed) return false;
    if (!find_dvar("aae_coop_pause_remote")) set_string("aae_coop_pause_remote","0",true);
    atomic_store(&mode,PAUSE_CHANNEL);
    return true;
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
    // Validate every transport entry before enabling the current AAE channel.
    const uint8_t send_prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    const uint8_t local_prefix[]={0x55,0x48,0x89,0xe5,0x48,0x63,0xf7};
    if (!memcmp(prepatch_original_code(address(T7O_MAC_AAE_GET_PACKET),sizeof(send_prefix)),send_prefix,sizeof(send_prefix)) &&
        !memcmp(prepatch_original_code(address(T7O_MAC_AAE_SEND_PACKET),sizeof(send_prefix)),send_prefix,sizeof(send_prefix)) &&
        !memcmp(prepatch_original_code(address(T7O_MAC_AAE_LOCAL_SOCKET),sizeof(local_prefix)),local_prefix,sizeof(local_prefix))) {
        send_packet=address(T7O_MAC_AAE_SEND_PACKET);local_socket=address(T7O_MAC_AAE_LOCAL_SOCKET);
        channel_installed=DobbyHook(address(T7O_MAC_AAE_GET_PACKET),(void *)channel_get_packet,(void **)&original_get_packet)==0;
    }
    installed=true;return true;
}
