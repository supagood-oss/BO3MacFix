#include "t7o_mac_script.h"
#include "t7o_mac_offsets.h"
#include "t7o_core.h"
#include "prepatch.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif

/* Recovered from the exact Mac binary, not Windows structure casts. These
 * adapters and the engine's thread/lifetime assumptions still need Mac tests. */
typedef struct { uint64_t value; uint32_t type, padding; } ScriptValue;
typedef struct {
    uint32_t hash, min_args, max_args, padding;
    void (*function)(unsigned);
    uint32_t kind, padding2;
} Builtin;
_Static_assert(sizeof(ScriptValue)==16,"Mac script value size");
_Static_assert(sizeof(Builtin)==0x20 && offsetof(Builtin,function)==0x10,"Mac builtin layout");

static uint64_t base;
static bool installed,active;
static int32_t (*engine_get_int)(unsigned,unsigned);
static const char *(*engine_get_string)(unsigned,unsigned);
static void (*engine_push_int)(unsigned,int32_t);
static void (*engine_push_vector)(unsigned,const float *);
static void (*engine_error)(unsigned,const char *,bool);
static void (*engine_view_origin)(const void *,float *,int);
static bool (*engine_dual_wield)(uint64_t);
static void (*engine_weapon_hand)(void *,int,unsigned);
static void (*engine_weapon_both)(void *,int);
static int (*engine_body_anim)(void *,int,int,int,int,int,int);
static int (*engine_anim_event)(void *,int,int,int,void *);
static void *(*engine_anim_data)(void);
static void (*original_profile)(unsigned);

#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t u32(const void *pointer) { uint32_t value;memcpy(&value,pointer,4);return value; }
static void *ptr(const void *pointer) { void *value;memcpy(&value,pointer,sizeof(value));return value; }
static uint8_t *vm_public(unsigned instance) {
    return instance<2 ? (uint8_t *)address(T7O_MAC_VM_PUBLIC)+(size_t)instance*T7O_MAC_VM_INSTANCE_STRIDE : NULL;
}
static const ScriptValue *argument(unsigned instance,unsigned index) {
    uint8_t *pub=vm_public(instance);
    if (!pub || index>=u32(pub+0x38)) return NULL;
    const ScriptValue *top=ptr(pub+0x20);
    return top ? top-index : NULL;
}
static bool get_int(void *context,int instance,unsigned index,int32_t *value) {
    (void)context;
    const ScriptValue *arg=argument((unsigned)instance,index);
    if (!arg || (arg->type!=5 && arg->type!=7)) return false;
    *value=engine_get_int((unsigned)instance,index);return true;
}
static const char *get_string(void *context,int instance,unsigned index) {
    (void)context;
    const ScriptValue *arg=argument((unsigned)instance,index);
    /* Hash operation callers pass ordinary strings. Do not cast arbitrary
     * vectors, localized strings or objects behind the caller's back. */
    return arg && arg->type==2 ? engine_get_string((unsigned)instance,index) : NULL;
}
static bool push_int(void *context,int instance,int32_t value) {
    (void)context;engine_push_int((unsigned)instance,value);return true;
}
static bool push_vector(void *context,int instance,const float value[3]) {
    (void)context;engine_push_vector((unsigned)instance,value);return true;
}
static uint8_t *client_globals(int local_client) {
    int count=(int)u32(address(T7O_MAC_CG_COUNT));
    uint8_t *array=ptr(address(T7O_MAC_CG_ARRAY));
    if (!array || local_client<0 || local_client>=count || local_client>1) return NULL;
    return array+(size_t)local_client*T7O_MAC_CG_STRIDE;
}
static bool view_origin(void *context,int instance,int local_client,float result[3]) {
    (void)context;(void)instance;
    uint8_t *cg=client_globals(local_client);
    if (!cg) return false;
    engine_view_origin(cg+T7O_MAC_CG_PLAYER_STATE,result,(int)u32(cg+T7O_MAC_CG_TIME));
    return isfinite(result[0]) && isfinite(result[1]) && isfinite(result[2]);
}
static bool weapon_animation(void *context,int instance,int local_client,int anim,int hand) {
    (void)context;(void)instance;
    uint8_t *cg=client_globals(local_client);
    if (!cg || hand<0 || hand>1) return false;
    uint8_t *ps=cg+T7O_MAC_CG_PLAYER_STATE;
    uint64_t weapon;memcpy(&weapon,ps+0x2c0,sizeof(weapon));
    if (engine_dual_wield(weapon)) engine_weapon_hand(ps,anim,(unsigned)hand);
    else engine_weapon_both(ps,anim);
    return true;
}
static bool body_animation(void *context,int instance,int local_client,int anim,int body_part) {
    (void)context;(void)instance;
    uint8_t *cg=client_globals(local_client);
    if (!cg || anim<0 || anim>=5120 || body_part<1 || body_part>3) return false;
    /* Match the routine's two dereferences after resolving its Mach-O TLS
     * descriptor. The loaded count follows 5120 128-byte definitions. */
    void *descriptor=address(T7O_MAC_ANIMATION_TLS_DESCRIPTOR);
    void *(*resolve)(void *)=NULL;memcpy(&resolve,descriptor,sizeof(resolve));
    void *slot=resolve ? resolve(descriptor) : NULL;
    void *thread_context=slot ? ptr(slot) : NULL;
    uint8_t *definitions=thread_context ? ptr(thread_context) : NULL;
    uint32_t count=definitions ? u32(definitions+0xa0000) : 0;
    if (!count || count>5120 || (uint32_t)anim>=count) return false;
    engine_body_anim(cg+T7O_MAC_CG_PLAYER_STATE,anim,body_part,0,0,1,1);
    return true;
}
static bool animation_event(void *context,int instance,int entity,int event) {
    (void)context;(void)instance;
    /* BG_AnimParseAnimScript clears 0x26730 bytes of 0x608-byte event
     * records: exactly 102 events. The original helper lacks this guard. */
    if (entity<0 || entity>=2048 || event<0 || event>=102) return false;
    uint8_t *ent=(uint8_t *)address(T7O_MAC_SERVER_ENTITIES)+(size_t)entity*T7O_MAC_SERVER_ENTITY_STRIDE;
    uint8_t *client=ptr(ent+0x250);uint16_t type;memcpy(&type,ent+0x1a0,2);
    /* The original AAE helper returns without side effects for an absent
     * server entity (including guest-only clients) or a non-player entity. */
    if (!client || type!=1) return true;
    int client_num=(int)u32(client);
    if (client_num<0 || client_num>=18) return false;
    uint8_t *clients=ptr(address(T7O_MAC_SERVER_ANIM_CLIENTS_GOT));
    void *data=engine_anim_data();
    if (!clients || !data) return false;
    engine_anim_event(clients+(size_t)client_num*T7O_MAC_SERVER_ANIM_CLIENT_STRIDE,event,0,1,data);
    return true;
}
static const T7oScriptBackend backend={NULL,get_int,get_string,push_int,push_vector,
    view_origin,animation_event,weapon_animation,body_animation};
static void dispatch(unsigned instance) {
    uint8_t *pub=vm_public(instance);
    if (!active || !pub || !u32(pub+0x38)) { original_profile(instance);return; }
    T7oScriptResult result=t7o_script_dispatch(&backend,(int)instance);
    /* The current DLL leaves unknown IDs without a result. Ordinary
     * IsProfileBuild() still calls the original zero-argument builtin. */
    if (result==T7O_SCRIPT_UNSUPPORTED) return;
    if (result!=T7O_SCRIPT_OK) engine_error(instance,"BO3MacFix: invalid or unavailable T7Overcharged script operation",false);
}
bool t7o_mac_script_active(void) { return active; }
bool t7o_mac_script_deactivate(void) {
    if (!active) return true;
    Builtin *entry=address(T7O_MAC_PROFILE_BUILTIN);active=false;
    if (entry->function==dispatch) { entry->function=original_profile;entry->max_args=0;return true; }
    return entry->function==original_profile && entry->max_args==0;
}
bool t7o_mac_script_activate(void) {
    if (!installed) return false;
    Builtin *entry=address(T7O_MAC_PROFILE_BUILTIN);
    if (active) return entry->function==dispatch;
    if (entry->hash!=t7o_script_hash("isprofilebuild") || entry->min_args!=0 ||
        entry->max_args!=0 || entry->function!=(void (*)(unsigned))address(T7O_MAC_PROFILE_FUNCTION)) return false;
    original_profile=entry->function;
    entry->max_args=255;
    entry->function=dispatch;
    active=true;return true;
}
bool t7o_mac_script_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    /* Exact-file validation is provided by the FX install path before this
     * call. These entry bytes additionally catch an overlapping runtime hook. */
    const uint32_t functions[]={T7O_MAC_VM_GET_INT,T7O_MAC_VM_GET_STRING,T7O_MAC_VM_PUSH_INT,
        T7O_MAC_VM_PUSH_VECTOR,T7O_MAC_VM_ERROR,T7O_MAC_PROFILE_FUNCTION,T7O_MAC_PLAYER_VIEW_ORIGIN,
        T7O_MAC_WEAPON_DUAL_WIELD,T7O_MAC_WEAPON_ANIM_HAND,T7O_MAC_WEAPON_ANIM_BOTH,
        T7O_MAC_BODY_PLAY_ANIM,T7O_MAC_ANIMATION_EVENT,T7O_MAC_ANIMATION_DATA};
    const uint8_t prologue[]={0x55,0x48,0x89,0xe5};
    for (size_t i=0;i<sizeof(functions)/sizeof(functions[0]);++i)
        if (memcmp(prepatch_original_code(address(functions[i]),sizeof(prologue)),prologue,sizeof(prologue))) return false;
    engine_get_int=(void *)address(T7O_MAC_VM_GET_INT);
    engine_get_string=(void *)address(T7O_MAC_VM_GET_STRING);
    engine_push_int=(void *)address(T7O_MAC_VM_PUSH_INT);
    engine_push_vector=(void *)address(T7O_MAC_VM_PUSH_VECTOR);
    engine_error=(void *)address(T7O_MAC_VM_ERROR);
    engine_view_origin=(void *)address(T7O_MAC_PLAYER_VIEW_ORIGIN);
    engine_dual_wield=(void *)address(T7O_MAC_WEAPON_DUAL_WIELD);
    engine_weapon_hand=(void *)address(T7O_MAC_WEAPON_ANIM_HAND);
    engine_weapon_both=(void *)address(T7O_MAC_WEAPON_ANIM_BOTH);
    engine_body_anim=(void *)address(T7O_MAC_BODY_PLAY_ANIM);
    engine_anim_event=(void *)address(T7O_MAC_ANIMATION_EVENT);
    engine_anim_data=(void *)address(T7O_MAC_ANIMATION_DATA);
    installed=true;return true;
}
