#include "t7o_mac_traversal.h"
#include "t7o_mac_offsets.h"
#include "t7o_core.h"
#include "t7o_inline.h"
#include "prepatch.h"
#include "dobby.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
typedef struct { float x,y,z; } Point;
static uint64_t base;
static bool installed;
static atomic_bool active;
/* SysV passes this three-float aggregate in XMM0/XMM1. The Windows
 * counterpart takes a vector pointer; that ABI must not be copied here. */
static int (*original_type)(const void *,Point);
static float (*original_setting)(const char *,bool);
static int (*vehicle_permission)(const void *,const void *);
static void *(*find_dvar)(const char *);
static const char *(*dvar_text)(const void *);
static void (*set_string)(const char *,const char *,bool);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uint16_t read16(const void *p) { uint16_t v;memcpy(&v,p,2);return v; }
static void *pointer(const void *p) { void *v;memcpy(&v,p,sizeof(v));return v; }
static float configured(const char *name,float fallback) {
    void *variable=find_dvar(name);
    const char *text=variable ? dvar_text(variable) : NULL;
    if (!text) return fallback;
    char *end;float value=strtof(text,&end);
    return end!=text && !*end && isfinite(value) ? value : fallback;
}
static int threshold(void) {
    float value=configured("aae_t7_move_input_threshold",0);
    return (int)fminf(256,fmaxf(-1,value));
}
static int traversal_type(const void *player,Point point) {
    if (!atomic_load(&active) || !player) return original_type(player,point);
    float z;memcpy(&z,(const uint8_t *)player+0x38,4);
    float height=point.z-z;
    if (!isfinite(height)) return original_type(player,point);
    return t7o_traversal_type(read32((const uint8_t *)player+0x7f4),height,
                              configured("aae_t7_high_height_offset",-18));
}
static float game_setting_for(const char *name,bool session,uintptr_t caller) {
    if (atomic_load(&active) && caller==base+T7O_MAC_TRAVERSAL_HEIGHT_RETURN &&
        name && !strcmp(name,"trm_maxHeight")) {
        float height=configured("aae_t7_mantle_height",57);
        return height>=0 && height<=4096 ? height : 57;
    }
    return original_setting(name,session);
}
static float game_setting(const char *name,bool session) {
    return game_setting_for(name,session,(uintptr_t)__builtin_return_address(0));
}
/* -1 means the server vehicle context is unavailable, as in the original
 * plugin. Predicted clients then retain tracing; an authoritative mismatch
 * denies the vault. Mac gclient is 0x17210 bytes, not Windows' 0x171f0. */
static int vault_permission(const uint8_t *trace,const uint8_t *player,const uint8_t *movement) {
    unsigned server=movement[0x2ac];
    if (server>1 || read32(trace+0x24)!=1) return -1;
    unsigned hit=read16(trace+0x28);
    if (hit>=0x3fe) return -1;
    uint8_t *entities=address(T7O_MAC_SERVER_ENTITIES);
    uint8_t *vehicle=entities+hit*T7O_MAC_SERVER_ENTITY_STRIDE;
    if (read16(vehicle+0x1a0)!=15) return -1;
    uint8_t *clients=pointer(address(T7O_MAC_SERVER_CLIENTS));
    if (!clients) return -1;
    unsigned client=read32(player);
    if (server) {
        uintptr_t delta=(uintptr_t)player-(uintptr_t)clients;
        if ((uintptr_t)player<(uintptr_t)clients || delta%T7O_MAC_SERVER_CLIENT_STRIDE ||
            delta/T7O_MAC_SERVER_CLIENT_STRIDE>=64 || delta/T7O_MAC_SERVER_CLIENT_STRIDE!=client) return 0;
    } else if (client>=64 || read32(clients+(size_t)client*T7O_MAC_SERVER_CLIENT_STRIDE+0x16bf8)!=client) return -1;
    uint8_t *entity=entities+client*T7O_MAC_SERVER_ENTITY_STRIDE;
    if (pointer(entity+0x250)!=clients+(size_t)client*T7O_MAC_SERVER_CLIENT_STRIDE) return server ? 0 : -1;
    return pointer(vehicle+0x270) ? vehicle_permission(vehicle,entity)!=4 : -1;
}
void t7o_mac_traversal_inline_dispatch(T7oInlineContext *context,unsigned operation) {
    if (!atomic_load(&active)) return;
    if (operation==T7O_INLINE_TRAVERSAL_INPUT) {
        /* Only the cached absolute-input sum changes, never command axes.
         * A zero vector stays zero even when the threshold is negative. */
        uint32_t sum=(uint32_t)context->r12;
        context->r12=sum && (int)sum>threshold() ? 51 : 0;
    } else if (operation==T7O_INLINE_TRAVERSAL_ELIGIBLE) {
        uint32_t sum=(uint32_t)context->rax+(uint32_t)context->rcx;
        int limit=threshold();uint32_t wanted=limit<=0 || (int)sum>limit ? 51 : 0;
        context->rcx=(uint32_t)(wanted-(uint32_t)context->rax);
    } else if (operation==T7O_INLINE_TRAVERSAL_SURFACE) {
        uint8_t *trace=(uint8_t *)(uintptr_t)(context->rbp-0x160);
        uint8_t *player=(void *)(uintptr_t)context->r14;
        const uint8_t *movement=(const void *)(uintptr_t)context->r13;
        uint32_t flags=read32(trace+0x18);
        bool ignore=configured("aae_t7_ignore_mantle_checks",0)!=0;
        int permission=ignore ? -1 : vault_permission(trace,player,movement);
        flags=permission==0 ? flags|0x1000000 : flags&~UINT32_C(0x1000000);
        memcpy(trace+0x18,&flags,4);
        if (permission!=0 && (flags&0x38000000)==0x18000000) {
            uint32_t state=read32(player+0x7f4)|1;memcpy(player+0x7f4,&state,4);
        }
    }
}
void t7o_mac_traversal_deactivate(void) {
    atomic_store(&active,false);
    if (!installed) return;
    prepatch_set_inline_callback(T7O_MAC_TRAVERSAL_INPUT,NULL);
    prepatch_set_inline_callback(T7O_MAC_TRAVERSAL_ELIGIBLE,NULL);
    prepatch_set_inline_callback(T7O_MAC_TRAVERSAL_SURFACE,NULL);
}
bool t7o_mac_traversal_activate(void) {
    if (!installed) return false;
    if (!prepatch_set_inline_callback(T7O_MAC_TRAVERSAL_INPUT,t7o_inline_traversal_input) ||
        !prepatch_set_inline_callback(T7O_MAC_TRAVERSAL_ELIGIBLE,t7o_inline_traversal_eligible) ||
        !prepatch_set_inline_callback(T7O_MAC_TRAVERSAL_SURFACE,t7o_inline_traversal_surface)) {
        t7o_mac_traversal_deactivate();return false;
    }
    const char *defaults[][2]={{"aae_t7_ignore_mantle_checks","0"},{"aae_t7_mantle_height","57"},
        {"aae_t7_high_height_offset","-18"},{"aae_t7_move_input_threshold","1"}};
    for (unsigned i=0;i<sizeof(defaults)/sizeof(*defaults);++i)
        if (!find_dvar(defaults[i][0])) set_string(defaults[i][0],defaults[i][1],true);
    atomic_store(&active,true);return true;
}
bool t7o_mac_traversal_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t classifier[]={0x55,0x48,0x89,0xe5,0xf3,0x0f,0x5c,0x4f,0x38};
    const uint8_t setting[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    if (memcmp(prepatch_original_code(address(T7O_MAC_TRAVERSAL_TYPE),sizeof(classifier)),classifier,sizeof(classifier)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_GAME_SETTING),sizeof(setting)),setting,sizeof(setting)) ||
        !prepatch_native_site_present(T7O_MAC_TRAVERSAL_INPUT) ||
        !prepatch_native_site_present(T7O_MAC_TRAVERSAL_ELIGIBLE) ||
        !prepatch_native_site_present(T7O_MAC_TRAVERSAL_SURFACE)) return false;
    find_dvar=address(T7O_MAC_DVAR_FIND_NAME);dvar_text=address(T7O_MAC_DVAR_VALUE_STRING);
    set_string=address(T7O_MAC_SET_DVAR_STRING);vehicle_permission=address(T7O_MAC_VEHICLE_PERMISSION);
    if (DobbyHook(address(T7O_MAC_TRAVERSAL_TYPE),(void *)traversal_type,(void **)&original_type)!=0 ||
        DobbyHook(address(T7O_MAC_GAME_SETTING),(void *)game_setting,(void **)&original_setting)!=0) return false;
    installed=true;return true;
}
