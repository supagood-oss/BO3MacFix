#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint64_t now=1000;
static uint64_t time_mock(void) { return now; }
static uint64_t nonce_mock(void) { static uint64_t next=0x40; return ++next; }
static void *test_address(uint32_t);
#define T7O_COOP_MILLISECONDS time_mock
#define T7O_AAE_NONCE nonce_mock
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_cooppause.c"
typedef struct { const char *name,*text; } Dvar;
static Dvar dvars[]={{"mapname","zm_test"},{"aae_coop_pause_remote","1"},{"sv_running","1"},
    {"cl_paused","1"},{"sv_maxclients","2"},{"aae_coop_paused","0"}};
static void *running=&dvars[2],*paused=&dvars[3],*maximum=&dvars[4];
static uint8_t client_data[T7O_MAC_SV_CLIENT_STRIDE*2],thread_data[32],args[0x80];
static uint8_t *clients=client_data;
static uint8_t guest_connection[0x25780],*connections=guest_connection;
static uint32_t connection_state=9;
static int server_msec,client_msec,controller_seen,checks;
static float scale_seen;
static char sent[32],tokenized[128];
static void *test_address(uint32_t rva) {
    switch (rva) {
    case T7O_MAC_SV_RUNNING_DVAR:return &running;
    case T7O_MAC_CL_PAUSED_DVAR:return &paused;
    case T7O_MAC_SV_MAXCLIENTS_DVAR:return &maximum;
    case T7O_MAC_SV_CLIENTS:return &clients;
    case T7O_MAC_CLIENT_CONNECTIONS:return &connections;
    case T7O_MAC_AAE_CONNECTION_STATE:return &connection_state;
    default:assert(0);return NULL;
    }
}
static void put32(void *p,uint32_t value) { memcpy(p,&value,4); }
static void *find_mock(const char *name) { for (unsigned i=0;i<sizeof(dvars)/sizeof(*dvars);++i) if (!strcmp(name,dvars[i].name)) return &dvars[i];return NULL; }
static const char *text_mock(const void *v) { return ((const Dvar *)v)->text; }
static void set_mock(const char *name,const char *value,bool force) { assert(force);Dvar *v=find_mock(name);assert(v);v->text=!strcmp(value,"1") ? "1" : "0"; }
static void *thread_mock(void) { return thread_data; }
static void send_mock(void *p,int reliable,const char *text) {
    assert(p==client_data+T7O_MAC_SV_CLIENT_STRIDE && reliable==1);
    snprintf(sent,sizeof(sent),"%s",text);put32((uint8_t *)p+0x5030,read32((uint8_t *)p+0x5030)+1);
}
static int server_mock(int msec) { server_msec=msec;return 123; }
static void client_mock(int controller,int msec,float scale) { controller_seen=controller;client_msec=msec;scale_seen=scale; }
static bool check_mock(void) { ++checks;return true; }
static void tokenize_mock(const char *text) { snprintf(tokenized,sizeof(tokenized),"%s",text); }
static unsigned receives,sent_count;
static uint8_t incoming[T7O_AAE_PAUSE_MESSAGE_SIZE+1];
static T7oAaePauseAddress host_address={3,7,0,4,4};
static int packet_mock(T7oAaePauseAddress *from,AaeMacMessage *message) {
    // Check the adapter restores the pointer before asking the engine again.
    assert(message->data==incoming);
    *from=host_address;
    if (receives++==0) {
        message->data=incoming+1;
        message->size=T7O_AAE_PAUSE_MESSAGE_SIZE;
    } else {
        message->size=4; // unrelated packet flows through
    }
    message->target_socket=0;
    return 1;
}
static bool channel_send_mock(int socket,int len,const void *bytes,T7oAaePauseAddress to) {
    const uint8_t *message=bytes;
    assert(socket==0 && len==T7O_AAE_PAUSE_MESSAGE_SIZE && to.ip==host_address.ip);
    assert(message[12]==(sent_count==0 ? 1 : 3));
    ++sent_count;
    return true;
}
static int socket_mock(int controller) { assert(controller==0);return 0; }
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;assert(0);return NULL; }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;assert(0);return -1; }
int main(void) {
    find_dvar=find_mock;dvar_text=text_mock;set_string=set_mock;thread_context=thread_mock;send_command=send_mock;
    original_server_frame=server_mock;original_client_frame=client_mock;original_check=check_mock;original_tokenize=tokenize_mock;
    void *a=args;memcpy(thread_data+0x18,&a,sizeof(a));put32(args+0x64,3);
    put32(client_data,5);put32(client_data+0x34,2);
    uint8_t *remote=client_data+T7O_MAC_SV_CLIENT_STRIDE;put32(remote,5);put32(remote+0x34,3);
    assert(!t7o_mac_cooppause_install(0,false) && !t7o_mac_cooppause_activate());
    assert(server_frame(50)==123 && server_msec==50);client_frame(7,50,1.25f);assert(controller_seen==7 && client_msec==50 && scale_seen==1.25f);
    base=0x100000000;installed=true;assert(t7o_mac_cooppause_activate());
    assert(server_frame(50)==123 && server_msec==50 && !strcmp(sent,"q @ 1"));
    assert(!check() && !checks && t7o_mac_cooppause_force_menu());
    put32(remote+0x5034,read32(remote+0x5030));now+=10;
    assert(server_frame(50)==123 && server_msec==0);client_frame(7,50,1.25f);
    assert(client_msec==0 && scale_seen==0 && controller_seen==7);
    dvars[3].text="0";now+=10;server_frame(50);client_frame(7,50,1.25f);
    assert(server_msec==50 && client_msec==50 && scale_seen==1.25f && !strcmp(sent,"q @ 0"));
    dvars[2].text="0";now+=10;client_frame(7,50,1.25f);
    tokenize_for("q @ 1 000a",base+T7O_MAC_SERVER_TOKENIZE_RETURN);assert(!strcmp(tokenized,"q cl_paused \"1\""));
    client_frame(7,50,1.25f);assert(client_msec==0 && scale_seen==0);
    tokenize_for("q @ 0",base+T7O_MAC_SERVER_TOKENIZE_RETURN+1);assert(!strcmp(tokenized,"q @ 0"));
    client_frame(7,50,1.25f);assert(client_msec==0);
    tokenize_for("q @ 0",base+T7O_MAC_SERVER_TOKENIZE_RETURN);client_frame(7,50,1.25f);assert(client_msec==50);
    tokenize_for("q @ 1",base+T7O_MAC_SERVER_TOKENIZE_RETURN);dvars[3].text="1";
    now+=5000;client_frame(7,50,1.25f);
    assert(client_msec==50 && !strcmp(dvars[3].text,"0") && !strcmp(dvars[5].text,"0"));
    dvars[0].text="core_frontend";tokenize_for("q @ 1",base+T7O_MAC_SERVER_TOKENIZE_RETURN);assert(!strcmp(tokenized,"q @ 1"));
    char name[66];memset(name,'A',64);name[64]=0;assert(t7o_coop_pause_map_active(name));name[64]='A';name[65]=0;assert(!t7o_coop_pause_map_active(name));
    t7o_mac_cooppause_deactivate();tokenize_for("q @ 1",base+T7O_MAC_SERVER_TOKENIZE_RETURN);client_frame(7,50,1.25f);
    assert(client_msec==50 && scale_seen==1.25f && !strcmp(tokenized,"q @ 1"));

    assert(!t7o_mac_cooppause_activate_channel());
    channel_installed=true;original_get_packet=packet_mock;send_packet=channel_send_mock;local_socket=socket_mock;
    dvars[0].text="zm_test";dvars[2].text="0";
    memcpy(guest_connection+T7O_MAC_AAE_HOST_ADDRESS,&host_address,sizeof(host_address));
    assert(t7o_mac_cooppause_activate_channel());
    client_frame(0,50,1.25f);assert(client_msec==50);
    const uint8_t state_packet[T7O_AAE_PAUSE_MESSAGE_SIZE]={
        0xe1,0,'A','A','E','C','P','1',[8]=1,[12]=2,[16]=1,[24]=0x41,[32]=1,[40]=1
    };
    /* Leave a spare routing byte because NET_GetPacket advances the buffer. */
    AaeMacMessage message={.data=incoming};
    T7oAaePauseAddress from={0};
    memcpy(incoming+1,state_packet,sizeof(state_packet));
    assert(channel_get_packet(&from,&message)==1 && receives==2 && message.size==4 && sent_count==1);
    client_frame(0,50,1.25f);
    assert(client_msec==0 && scale_seen==0 && !strcmp(dvars[3].text,"1"));
    assert(channel_get_packet(&from,&message)==1 && sent_count==2);
    t7o_mac_cooppause_deactivate();
    puts("Mac AAE co-op pause: Mac client layout, reliable send/ACK, server/client frame arguments, command caller scope, menu and unload fallback passed (substituted engine)");
}
