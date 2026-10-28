#include "t7o_mac_pause.h"
#include "t7o_mac_offsets.h"
#include "prepatch.h"
#include "dobby.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static bool (*original_client)(void),(*original_server)(void);
static bool (*session_mode)(int),(*demo_playing)(void),(*server_paused)(void);
static int (*pending_count)(void);
static void *(*dvar_find)(const char *);
static const char *(*dvar_string)(const void *);
static void *address(uint32_t offset) { return (void *)(uintptr_t)(base+offset); }
static bool requested(void) {
    if (!atomic_load(&active)) return false;
    void *variable=dvar_find("hook_pausegame");
    const char *value=variable ? dvar_string(variable) : NULL;
    return value && strtol(value,NULL,10)==1;
}
static bool client_can_pause(void) {
    if (!requested()) return original_client();
    if (session_mode(3) || demo_playing()) return false;
    /* AAE's native helper additionally makes a pure lobby-state query with
     * argument 1 and ignores its return. It does not set paused state. */
    return true;
}
static bool server_can_pause(void) {
    if (!requested()) return original_server();
    if (session_mode(3) || pending_count()>0) return false;
    return server_paused();
}
bool t7o_mac_pause_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t client[]={0x55,0x48,0x89,0xe5,0x48,0x8d,0x0d,0x4a,0xc1,0x04,0x10};
    const uint8_t server[]={0x55,0x48,0x89,0xe5,0x53};
    if (memcmp(prepatch_original_code(address(T7O_MAC_CLIENT_CAN_PAUSE),sizeof(client)),client,sizeof(client)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_SERVER_CAN_PAUSE),sizeof(server)),server,sizeof(server))) return false;
    session_mode=(void *)address(T7O_MAC_SESSION_IS_MODE);
    demo_playing=(void *)address(T7O_MAC_DEMO_PLAYING);
    server_paused=(void *)address(T7O_MAC_SERVER_IS_PAUSED);
    pending_count=(void *)address(T7O_MAC_PENDING_COUNT);
    dvar_find=(void *)address(T7O_MAC_DVAR_FIND_NAME);
    dvar_string=(void *)address(T7O_MAC_DVAR_VALUE_STRING);
    if (DobbyHook(address(T7O_MAC_CLIENT_CAN_PAUSE),(void *)client_can_pause,(void **)&original_client)!=0 ||
        DobbyHook(address(T7O_MAC_SERVER_CAN_PAUSE),(void *)server_can_pause,(void **)&original_server)!=0) return false;
    installed=true;return true;
}
bool t7o_mac_pause_activate(void) {
    if (!installed) return false;
    atomic_store(&active,true);return true;
}
void t7o_mac_pause_deactivate(void) { atomic_store(&active,false); }
