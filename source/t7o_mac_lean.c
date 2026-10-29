#include "t7o_mac_lean.h"
#include "t7o_mac_offsets.h"
#include "prepatch.h"
#include "dobby.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
typedef struct { float from[3],target[3],current[3],elapsed,duration;unsigned curve,tag;int time;bool moving,engaged; } Lean;
static Lean clients[2];
static uint64_t base;
static bool installed;
static atomic_bool active;
static void (*original_key)(int,int,int,int);
static void (*original_bones)(void *,void *,void *,void *,void *);
static int (*key_down)(int,int);
static bool (*main_thread)(void);
static unsigned (*alloc_string)(const char *);
static int (*bone_index)(void *,unsigned,uint16_t *,unsigned);
static void (*set_tag)(void *,const uint32_t *,uint16_t,const float *,const float *);
static void *(*find_dvar)(const char *);
static const char *(*dvar_text)(const void *);
static void (*set_string)(const char *,const char *,bool);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static int32_t read32(const void *p) { int32_t v;memcpy(&v,p,4);return v; }
static const char *setting(const char *name) {
    void *dvar=find_dvar(name);return dvar ? dvar_text(dvar) : NULL;
}
static int integer(const char *name,int fallback) {
    const char *text=setting(name);if (!text) return fallback;
    char *end;long value=strtol(text,&end,10);
    return end!=text && !*end && value>=INT32_MIN && value<=INT32_MAX ? (int)value : fallback;
}
static void retarget(Lean *state,const float angles[3]) {
    bool different=false;
    for (unsigned i=0;i<3;++i) different|=fabsf(state->current[i]-angles[i])>=0.01f;
    memcpy(state->from,state->current,sizeof(state->from));
    for (unsigned i=0;i<3;++i) state->target[i]=different ? angles[i] : 0;
    state->duration=different ? 1.0f : 0.6f;state->curve=different ? 3 : 2;
    state->elapsed=0;state->moving=true;state->engaged=true;
}
static void step(Lean *state) {
    if (!state->moving) return;
    /* The current native image advances 0.075 per bone callback. Do not
     * silently replace its easing with a guessed wall-clock duration. */
    state->elapsed+=0.075f;
    float t=fminf(1,state->elapsed/state->duration),weight;
    if (state->curve==2) weight=1-(1-t)*(1-t);
    else weight=t<0.5f ? 2*t*t : 1-2*(1-t)*(1-t);
    for (unsigned i=0;i<3;++i) state->current[i]=state->from[i]+(state->target[i]-state->from[i])*weight;
    if (t>=1) { memcpy(state->current,state->target,sizeof(state->current));state->moving=false; }
}
static bool angles_setting(float angles[3]) {
    const char *text=setting("lean_ang");if (!text) { angles[0]=angles[1]=0;angles[2]=25;return true; }
    for (unsigned i=0;i<3;++i) {
        char *end;float value=strtof(text,&end);
        if (end==text || !isfinite(value)) return false;
        angles[i]=fminf(180,fmaxf(-180,value));text=end;
    }
    while (*text==' ' || *text=='\t') ++text;
    return !*text;
}
static void key_event(int client,int key,int down,int time) {
    /* Expanded handles key-up events before the engine updates its key table.
     * Its left/right keys rotate the weapon; the fire key clears the lean. */
    if (atomic_load(&active) && client>=0 && client<2 && !down && main_thread()) {
        const uint8_t *ui=(const uint8_t *)address(T7O_MAC_CLIENT_UI)+(size_t)client*0x1078;
        if (!(ui[4]&1) && read32(ui+8)==11 && integer("leaning_enabled",1)) {
            float angles[3];
            if (angles_setting(angles)) {
                int left=integer("leaning_key_left",0),right=integer("leaning_key_right",0);
                if (left>0 && left<256 && key_down(client,left)) {
                    float previous=angles[2];if (angles[2]>0) angles[2]=-angles[2];
                    retarget(&clients[client],angles);angles[2]=previous;
                }
                if (right>0 && right<256 && key_down(client,right)) retarget(&clients[client],angles);
                if (key_down(client,160)) { const float zero[3]={0};retarget(&clients[client],zero); }
            }
        }
    }
    original_key(client,key,down,time);
}
static void update_bones(void *cg,void *player,void *object,void *weapon,void *entity) {
    original_bones(cg,player,object,weapon,entity);
    if (!atomic_load(&active) || !cg || !object || !main_thread()) return;
    int client=read32((const uint8_t *)cg+4);
    if (client<0 || client>=2 || !integer("leaning_enabled",1)) return;
    Lean *state=&clients[client];int now=read32((const uint8_t *)cg+T7O_MAC_CG_TIME);
    if (now<state->time) memset(state,0,sizeof(*state));
    state->time=now;
    /* Other T7Overcharged mods share this adapter. Leave their normal bone
     * animation alone until a configured lean key actually requests a lean. */
    if (!state->engaged) return;
    step(state);
    /* GScr_AllocString assigns the engine's level-owned string user, exactly
     * as its own static tag initialization does. Reacquire after time resets. */
    if (!state->tag) state->tag=alloc_string("tag_ads");
    uint16_t bone=UINT16_C(0xfffe);
    if (state->tag && bone_index(object,state->tag,&bone,UINT32_MAX)) {
        const uint32_t bits[12]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,
            UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
        const float origin[3]={0};set_tag(object,bits,bone,origin,state->current);
        if (!state->moving && !state->current[0] && !state->current[1] && !state->current[2]) state->engaged=false;
    }
}
void t7o_mac_lean_deactivate(void) { atomic_store(&active,false);memset(clients,0,sizeof(clients)); }
bool t7o_mac_lean_activate(void) {
    if (!installed) return false;
    const char *defaults[][2]={{"leaning_enabled","1"},{"leaning_key_left","0"},{"leaning_key_right","0"},{"lean_ang","0 0 25"}};
    for (unsigned i=0;i<sizeof(defaults)/sizeof(*defaults);++i)
        if (!find_dvar(defaults[i][0])) set_string(defaults[i][0],defaults[i][1],true);
    atomic_store(&active,true);return true;
}
bool t7o_mac_lean_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    if (memcmp(prepatch_original_code(address(T7O_MAC_KEY_EVENT),sizeof(prefix)),prefix,sizeof(prefix)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_VIEWMODEL_BONES),sizeof(prefix)),prefix,sizeof(prefix))) return false;
    find_dvar=address(T7O_MAC_DVAR_FIND_NAME);dvar_text=address(T7O_MAC_DVAR_VALUE_STRING);
    set_string=address(T7O_MAC_SET_DVAR_STRING);main_thread=address(T7O_MAC_IS_MAIN_THREAD);
    key_down=address(T7O_MAC_KEY_DOWN);alloc_string=address(T7O_MAC_ALLOC_LEVEL_STRING);
    bone_index=address(T7O_MAC_BONE_INDEX);set_tag=address(T7O_MAC_SET_LOCAL_TAG);
    if (DobbyHook(address(T7O_MAC_KEY_EVENT),(void *)key_event,(void **)&original_key)!=0 ||
        DobbyHook(address(T7O_MAC_VIEWMODEL_BONES),(void *)update_bones,(void **)&original_bones)!=0) return false;
    installed=true;return true;
}
