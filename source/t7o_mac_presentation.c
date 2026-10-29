#include "t7o_mac_presentation.h"
#include "t7o_mac_offsets.h"
#include "t7o_mac_freeaim.h"
#include "t7o_core.h"
#include "prepatch.h"
#include "dobby.h"
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static int last_status=-1;
static void (*original_hint)(int,const void *,void *,void *);
static bool (*original_crosshair)(const void *,const void *);
static uint64_t (*view_weapon)(void *);
static const uint8_t *(*weapon_definition)(uint64_t);
static int (*zoom_count)(uint64_t),(*zoom_level)(uint64_t);
static void (*set_string)(const char *,const char *,bool);
static void *(*find_dvar)(const char *);
static const char *(*dvar_text)(const void *);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static int32_t integer(const void *p) { int32_t v;memcpy(&v,p,4);return v; }
static bool crosshair_visible(const void *client,const void *player) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    t7o_mac_freeaim_seen(client,caller);
    if (atomic_load(&active)) {
        void *variable=find_dvar("t7o_force_3rd_person_crosshair");
        const char *value=variable ? dvar_text(variable) : NULL;
        if (value && strtol(value,NULL,10)!=0) return true;
    }
    return t7o_mac_freeaim_crosshair(client,caller,original_crosshair(client,player));
}
int t7o_mac_presentation_gamepad(int fallback) {
    if (!atomic_load(&active) || !find_dvar || !dvar_text) return fallback;
    void *dvar=find_dvar("t7o_override_gamepadtype");
    const char *value=dvar ? dvar_text(dvar) : NULL;
    if (!value) return fallback;
    char *end; long selected=strtol(value,&end,10);
    if (end==value || *end) return fallback;
    /* Mod values: 1 Xbox, 2 PlayStation, 3 Steam. Aspyr's enum differs. */
    return selected==1 ? 1 : selected==2 ? 0 : selected==3 ? 2 : fallback;
}
static void publish(int status) {
    if (last_status==status) return;
    char value[16];snprintf(value,sizeof(value),"%d",status);set_string("aim_status",value,true);last_status=status;
}
static void hold_breath_hint(int client,const void *rectangle,void *font,void *style) {
    original_hint(client,rectangle,font,style);
    if (!atomic_load(&active) || client<0 || client>=2 || client>=integer(address(T7O_MAC_CG_COUNT))) return;
    uint8_t *array;memcpy(&array,address(T7O_MAC_CG_ARRAY),sizeof(array));
    if (!array) return;
    uint8_t *cg=array+(size_t)client*T7O_MAC_CG_STRIDE,*ps=cg+T7O_MAC_CG_PLAYER_STATE;
    int status=0;
    if (!(ps[0x18]&4) && !(ps[0x15d]&0x40)) {
        uint64_t weapon=view_weapon(ps);const uint8_t *definition=weapon_definition(weapon);
        float fraction;memcpy(&fraction,ps+0x2fc,4);
        if (definition && integer(definition+0x70)!=10 && fraction==1.0f) {
            status=zoom_count(weapon) && zoom_level(weapon) ? 4 : 1;
            if (definition[0xdc8] && !definition[0xe67]) status=2;
        }
    }
    status=t7o_aim_status_tail(status,(ps[0x18]&4)!=0,integer(cg+T7O_MAC_BREATH_SPENT),
        integer(cg+T7O_MAC_BREATH_LIMIT),integer(cg+T7O_MAC_BREATH_RECOVERY),integer(cg+T7O_MAC_CG_FRAME_MSEC));
    publish(status);
}
bool t7o_mac_presentation_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    if (memcmp(prepatch_original_code(address(T7O_MAC_HOLD_BREATH_HINT),sizeof(prefix)),prefix,sizeof(prefix)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_CROSSHAIR_VISIBLE),sizeof(prefix)),prefix,sizeof(prefix))) return false;
    view_weapon=(void *)address(T7O_MAC_VIEW_WEAPON);weapon_definition=(void *)address(T7O_MAC_WEAPON_DEFINITION);
    zoom_count=(void *)address(T7O_MAC_WEAPON_ZOOM_COUNT);zoom_level=(void *)address(T7O_MAC_WEAPON_ZOOM_LEVEL);
    set_string=(void *)address(T7O_MAC_SET_DVAR_STRING);
    find_dvar=(void *)address(T7O_MAC_DVAR_FIND_NAME);dvar_text=(void *)address(T7O_MAC_DVAR_VALUE_STRING);
    if (DobbyHook(address(T7O_MAC_HOLD_BREATH_HINT),(void *)hold_breath_hint,(void **)&original_hint)!=0 ||
        DobbyHook(address(T7O_MAC_CROSSHAIR_VISIBLE),(void *)crosshair_visible,(void **)&original_crosshair)!=0) return false;
    installed=true;return true;
}
bool t7o_mac_presentation_activate(void) {
    if (!installed) return false;
    last_status=-1;atomic_store(&active,true);publish(0);return true;
}
void t7o_mac_presentation_deactivate(void) {
    atomic_store(&active,false);if (installed) publish(0);
}
