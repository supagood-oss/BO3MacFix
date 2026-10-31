#include "t7o_mac_mantle.h"
#include "t7o_mantle.h"
#include "t7o_mac_offsets.h"
#include "t7o_inline.h"
#include "prepatch.h"
#include "dobby.h"
#include <math.h>
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
static T7oMantleState state;
typedef struct { void *movement;unsigned client,server;bool pending,probe; } Scope;
static _Thread_local Scope scope;
static void (*original_move)(void *,void *);
static bool (*original_gate)(void *,void *);
static bool (*original_trace)(void *,void *,float *,float *);
static void (*original_hud)(int,const void *,void *,void *);
static void *(*find_dvar)(const char *);
static const char *(*dvar_text)(const void *);
static bool (*get_bool)(const void *);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t u32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uint64_t u64(const void *p) { uint64_t v;memcpy(&v,p,8);return v; }
static void *pointer(const void *p) { void *v;memcpy(&v,p,8);return v; }
#ifndef T7O_MANTLE_MILLISECONDS
static uint64_t milliseconds(void) {
    struct timespec t;if (clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
#define T7O_MANTLE_MILLISECONDS milliseconds
#endif
static bool context_enabled(void) {
    if (!atomic_load(&active) || (u32(address(T7O_MAC_SESSION_FLAGS))&0x3c0)!=0x40) return false;
    void *enabled=pointer(address(T7O_MAC_MANTLE_ENABLE));
    /* Use the Mac engine's own mantle enable policy. The additional Windows
     * integer-dvar address is not a valid Mac binding. Never guess its offset. */
    return enabled && get_bool(enabled);
}
static float height(void) {
    void *dvar=find_dvar("aae_t7_mantle_height");const char *text=dvar ? dvar_text(dvar):NULL;
    char *end;float value=text ? strtof(text,&end):57;
    return text && (end==text || *end || !isfinite(value) || value<0 || value>4096) ? 57:value;
}
static void track(void *movement) {
    const uint8_t *player=movement ? pointer(movement):NULL;if (!player) return;
    pthread_mutex_lock(&lock);t7o_mantle_track(&state,u32(player),(u64(player+0x10)&0x200)!=0);pthread_mutex_unlock(&lock);
}
static void move(void *movement,void *local) {
    if (!context_enabled() || !movement || !local || !pointer(movement)) { original_move(movement,local);return; }
    Scope previous=scope;scope=(Scope){.movement=movement,.client=u32(pointer(movement)),.server=((uint8_t *)movement)[0x2ac]};
    pthread_mutex_lock(&lock);t7o_mantle_height(&state,height());pthread_mutex_unlock(&lock);
    track(movement);original_move(movement,local);track(movement);scope=previous;
}
static bool gate(void *movement,void *local) {
    bool result=original_gate(movement,local);scope.pending=false;
    if (scope.movement!=movement || !context_enabled()) return result;
    track(movement);pthread_mutex_lock(&lock);
    bool eligible=t7o_mantle_probe(&state,scope.client,scope.server,T7O_MANTLE_MILLISECONDS());pthread_mutex_unlock(&lock);
    scope.pending=!result && eligible;return result || scope.pending;
}
static bool trace(void *movement,void *local,float *first,float *second) {
    if (!scope.pending || scope.movement!=movement || !context_enabled()) return original_trace(movement,local,first,second);
    scope.pending=false;bool previous=scope.probe;scope.probe=true;
    bool found=original_trace(movement,local,first,second);scope.probe=previous;
    pthread_mutex_lock(&lock);t7o_mantle_result(&state,scope.client,scope.server,T7O_MANTLE_MILLISECONDS(),found);pthread_mutex_unlock(&lock);
    return false; /* A hint probe must never start physical traversal. */
}
void t7o_mac_mantle_inline_dispatch(T7oInlineContext *context,unsigned operation) {
    if (operation!=T7O_INLINE_MANTLE_HEIGHT || !scope.probe || !context_enabled()) return;
    float z;memcpy(&z,context->xmm[4],4);z+=height();memcpy(context->xmm[4],&z,4);
}
static uint8_t *cg_for(int client) {
    uint8_t *array=pointer(address(T7O_MAC_CG_ARRAY));
    return array && client>=0 && client<2 && (unsigned)client<u32(address(T7O_MAC_CG_COUNT)) ? array+(size_t)client*T7O_MAC_CG_STRIDE:NULL;
}
static void hud(int client,const void *rectangle,void *font,void *style) {
    uint8_t *cg=context_enabled() ? cg_for(client):NULL;
    if (!cg) { original_hud(client,rectangle,font,style);return; }
    const uint8_t *player=cg+T7O_MAC_CG_PLAYER_STATE;unsigned entity=u32(player);
    uint32_t old=u32(cg+0x11b0b4),replacement=old;
    pthread_mutex_lock(&lock);t7o_mantle_height(&state,height());t7o_mantle_track(&state,entity,(u64(player+0x10)&0x200)!=0);
    replacement=t7o_mantle_ui(&state,entity,T7O_MANTLE_MILLISECONDS(),old);pthread_mutex_unlock(&lock);
    memcpy(cg+0x11b0b4,&replacement,4);original_hud(client,rectangle,font,style);
    if (u32(cg+0x11b0b4)==replacement) memcpy(cg+0x11b0b4,&old,4);
}
bool t7o_mac_mantle_ui_mode(int mode,uintptr_t caller,bool original) {
    return mode==0 && caller==base+T7O_MAC_MANTLE_MODE_RETURN && context_enabled() ? false:original;
}
void t7o_mac_mantle_deactivate(void) {
    atomic_store(&active,false);if (installed) prepatch_set_inline_callback(T7O_MAC_MANTLE_HEIGHT,NULL);
    pthread_mutex_lock(&lock);memset(&state,0,sizeof(state));pthread_mutex_unlock(&lock);memset(&scope,0,sizeof(scope));
}
bool t7o_mac_mantle_activate(void) {
    if (!installed) return false;
    if (atomic_load(&active)) return true;
    if (!prepatch_set_inline_callback(T7O_MAC_MANTLE_HEIGHT,t7o_inline_mantle_height)) return false;
    atomic_store(&active,true);return true;
}
bool t7o_mac_mantle_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    const uint32_t targets[]={T7O_MAC_MANTLE_MOVE,T7O_MAC_MANTLE_GATE,T7O_MAC_MANTLE_TRACE,T7O_MAC_MANTLE_HUD};
    for (unsigned i=0;i<4;++i) if (memcmp(prepatch_original_code(address(targets[i]),6),prefix,6)) return false;
    if (!prepatch_native_site_present(T7O_MAC_MANTLE_HEIGHT)) return false;
    find_dvar=address(T7O_MAC_DVAR_FIND_NAME);dvar_text=address(T7O_MAC_DVAR_VALUE_STRING);get_bool=address(T7O_MAC_DVAR_BOOL);
    if (DobbyHook(address(T7O_MAC_MANTLE_MOVE),(void *)move,(void **)&original_move) ||
        DobbyHook(address(T7O_MAC_MANTLE_GATE),(void *)gate,(void **)&original_gate) ||
        DobbyHook(address(T7O_MAC_MANTLE_TRACE),(void *)trace,(void **)&original_trace) ||
        DobbyHook(address(T7O_MAC_MANTLE_HUD),(void *)hud,(void **)&original_hud)) return false;
    installed=true;return true;
}
