#include "t7o_mac_movement.h"
#include "t7o_mac_offsets.h"
#include "t7o_core.h"
#include "t7o_movement.h"
#include "t7o_inline.h"
#include "prepatch.h"
#include "dobby.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <stddef.h>
#include <errno.h>
_Static_assert(offsetof(T7oInlineContext,r15)==264,"inline GPR offset");
_Static_assert(offsetof(T7oInlineContext,flags)==384 && offsetof(T7oInlineContext,return_address)==392 &&
    sizeof(T7oInlineContext)==400,"inline flags/return/frame size");
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static float (*original_strafe)(void);
static void (*original_sprint)(void *,void *);
static void (*original_slide)(void *,void *);
static void (*engine_accelerate)(void *,void *,const float *,float,float);
static void (*original_friction)(void *,void *);
static float (*original_friction_scale)(void);
static bool (*engine_airborne)(void *,void *);
static float (*original_jump_penalty)(void *);
static void (*original_apply_penalty)(void *);
static unsigned char (*original_jump)(void *,void *,float);
static int (*original_land_severity)(void *,float);
static bool (*original_slide_eligible)(void *);
static float (*engine_vector_yaw)(const float *);
static float (*engine_angle_delta)(float,float);
static void (*original_clamp_pitch)(void *,void *,int);
static void (*original_force_view)(void *,int,int,int,int,float);
static _Thread_local bool jump_crouched;
static _Thread_local unsigned jump_depth;
/* Physics calls may run in different prediction contexts. The Windows DLL's
 * global current-pmove pointer must not leak between Mac threads or nesting. */
static atomic_uint state_generation;
static _Thread_local unsigned local_generation;
static _Thread_local void *friction_movement;
typedef struct { void *player;uint64_t time; } AirTime;
static _Thread_local AirTime air_times[1024];
typedef struct { uintptr_t stack;void *player;uint8_t horizontal[8]; } Landing;
static _Thread_local Landing landings[16];
static uint64_t clock_ms(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC,&value)!=0) return UINT64_MAX;
    return (uint64_t)value.tv_sec*1000+(uint64_t)value.tv_nsec/1000000;
}
static atomic_bool patches[T7O_MOVEMENT_COUNT];
static void *(*dvar_find)(const char *);
static const char *(*dvar_string)(const void *);
static void *(*dvar_effective)(void *);
static void (*command_add)(const char *,void (*)(void),void *);
static void *(*command_find)(const char *);
static void *(*thread_context)(void);
static bool commands_registered;
static _Alignas(8) uint8_t command_records[3][0x30];
static int last_all;
static int last_enabled;
static void *address(uint32_t offset) { return (void *)(uintptr_t)(base+offset); }
static const char *setting(const char *name) {
    void *variable=dvar_find(name);
    return variable ? dvar_string(variable) : NULL;
}
static uint32_t ads_multiplier(void) {
    const char *value=setting("t7o_perk_fastads_multiplier");float scale=value ? strtof(value,NULL) : 2.15f;
    if (!isfinite(scale)) scale=2.15f;
    if (scale<1.0f) scale=1.0f;
    if (scale>3.0f) scale=3.0f;
    uint32_t bits;memcpy(&bits,&scale,4);return bits;
}
static bool enabled(void) {
    if (!atomic_load(&active)) return false;
    const char *value=setting("hook_movement_perks");
    return !value || strtol(value,NULL,10)==1;
}
static float strafe_scale(void) {
    if (!enabled()) return original_strafe();
    const char *value=setting("t7o_strafe_speed_scale");
    float configured=value ? strtof(value,NULL) : 0.667f;
    /* The native DLL registers this DVAR with a 0.667..0.97 domain.
     * Strings created by Lua before registration still need that bound. */
    if (isfinite(configured) && configured>0.97f) configured=0.97f;
    return t7o_strafe_scale(configured);
}
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static void *read_pointer(const void *p) { void *v;memcpy(&v,p,sizeof(v));return v; }
static const char *command_argument(unsigned index,unsigned *count) {
    uint8_t *thread=thread_context();uint8_t *args=thread ? read_pointer(thread+0x18) : NULL;
    if (!args) return NULL;
    int nesting=(int)read32(args);
    if (nesting<0 || nesting>=8) return NULL;
    *count=read32(args+0x64+4*nesting);
    if (*count>512 || index>=*count) return NULL;
    uint8_t *argv=read_pointer(args+0x88+8*nesting);
    return argv ? read_pointer(argv+index*sizeof(void *)) : NULL;
}
bool t7o_mac_movement_set(const char *name,bool enabled_value) {
    T7oMovementPatch patch;
    if (!installed || !t7o_movement_find(name,&patch)) return false;
    bool requested=enabled_value;enabled_value=enabled_value && enabled();
    bool ok=false;
    switch (patch) {
    case T7O_SLIDE_MOMENTUM:case T7O_JUMP_SLOWDOWN:case T7O_INSTANT_SLIDE:case T7O_SLIDE_ANGLES:
        ok=true;break;
    case T7O_SPRINT_LEAP:
        ok=prepatch_set_native_value(T7O_MAC_SPRINT_LEAP_COMPARE_A,500,enabled_value) &&
           prepatch_set_native_value(T7O_MAC_SPRINT_LEAP_COMPARE_B,500,enabled_value);break;
    case T7O_SPRINT_LERP:
        ok=prepatch_set_native_value(T7O_MAC_SPRINT_LERP_COMPARE,9,enabled_value);break;
    case T7O_SLIDE_THEN_SPRINT:
        ok=prepatch_set_native_value(T7O_MAC_SLIDE_SPRINT_BRANCH,1,enabled_value);break;
    case T7O_FAST_JUMP:
        ok=prepatch_set_native_value(T7O_MAC_JUMP_INTERVAL_COMPARE,1,enabled_value);break;
    case T7O_FAST_ADS:
        ok=prepatch_set_native_value(T7O_MAC_ADS_MODE_BRANCH_A,1,enabled_value) &&
           prepatch_set_native_value(T7O_MAC_ADS_MODE_BRANCH_B,1,enabled_value) &&
           prepatch_set_native_value(T7O_MAC_ADS_SCALE,ads_multiplier(),enabled_value);break;
    case T7O_FAST_SWAP:
        ok=prepatch_set_native_value(T7O_MAC_SWAP_TIMING_A,0x3ee38866,enabled_value) &&
           prepatch_set_native_value(T7O_MAC_SWAP_TIMING_B,0x3ee38866,enabled_value) &&
           prepatch_set_native_value(T7O_MAC_SWAP_ANIMATION,0x3ee38866,enabled_value) &&
           prepatch_set_native_value(T7O_MAC_SWAP_PRESENTATION,0x3ee38866,enabled_value);break;
    case T7O_SLIDE_SLOWDOWN:
        ok=prepatch_set_native_value(T7O_MAC_SLIDE_SLOW_BRANCH,1,enabled_value) &&
           prepatch_set_native_value(T7O_MAC_SLIDE_FRICTION_LOAD,0,enabled_value);break;
    default:return false;
    }
    if (ok) patches[patch]=requested;
    return ok;
}
bool t7o_mac_movement_set_all(bool enabled_value) {
    bool previous[T7O_MOVEMENT_COUNT];
    for (int i=0;i<T7O_MOVEMENT_COUNT;++i) previous[i]=patches[i];
    bool ok=true;
    for (int i=0;i<T7O_MOVEMENT_COUNT;++i)
        if (!t7o_mac_movement_set(t7o_movement_name((T7oMovementPatch)i),enabled_value)) { ok=false;break; }
    if (!ok) for (int i=0;i<T7O_MOVEMENT_COUNT;++i)
        t7o_mac_movement_set(t7o_movement_name((T7oMovementPatch)i),previous[i]);
    return ok;
}
static void command_patch(void) {
    unsigned count=0;const char *name=command_argument(1,&count),*value=command_argument(2,&count);
    if (count!=3 || !name || !value) return;
    if (!t7o_mac_movement_set(name,strtol(value,NULL,10)!=0))
        printf("T7Overcharged: movement patch is unavailable: %s\n",name);
}
static void command_all(void) {
    unsigned count=0;const char *value=command_argument(1,&count);
    if (count!=2 || !value) return;
    if (!t7o_mac_movement_set_all(strtol(value,NULL,10)!=0))
        printf("T7Overcharged: complete movement patch family is unavailable\n");
    const char *current=setting("movement_patch_all");last_all=current && strtol(current,NULL,10)!=0;
}
static bool set_dvar_flags(const char *name,unsigned count,const char *const *bits) {
    if (!atomic_load(&active) || !name || !*name || count>3) return false;
    uint32_t mask=0;
    for (unsigned i=0;i<count;++i) {
        if (!bits[i] || !*bits[i]) return false;
        char *end; errno=0; long value=strtol(bits[i],&end,10);
        if (errno || *end || value<INT32_MIN || value>INT32_MAX) return false;
        mask|=UINT32_C(1)<<((uint32_t)value&31);
    }
    uint8_t *variable=dvar_find(name);
    if (!variable) return false;
    if (read32(variable+0x1c)==15) variable=dvar_effective(variable);
    if (!variable) return false;
    /* Mac Dvar_RegisterNew/SetFromString confirm flags at +0x18, type +0x1c.
     * Arguments are bit positions, not a numeric mask. With none, clear flags. */
    memcpy(variable+0x18,&mask,sizeof(mask)); return true;
}
static void command_dvarflag(void) {
    unsigned count=0; const char *name=command_argument(1,&count);
    if (count<2 || count>5 || !name) return;
    const char *bits[3];
    for (unsigned i=2;i<count;++i) bits[i-2]=command_argument(i,&count);
    if (!set_dvar_flags(name,count-2,bits)) printf("T7Overcharged: dvarflag target or arguments unavailable\n");
}
typedef struct { void *player,*local; } AccelerationContext;
static bool fast_jump(void) { return enabled() && patches[T7O_FAST_JUMP]; }
static void reset_local_state(void) {
    unsigned generation=atomic_load(&state_generation);
    if (generation!=local_generation) {
        memset(air_times,0,sizeof(air_times));memset(landings,0,sizeof(landings));
        friction_movement=NULL;local_generation=generation;
    }
}
static void remember_air(void *player,uint64_t now) {
    if (now==UINT64_MAX) return;
    size_t selected=0;
    for (size_t i=0;i<1024;++i) {
        if (air_times[i].player==player || !air_times[i].player) { selected=i;break; }
        if (air_times[i].time<air_times[selected].time) selected=i;
    }
    air_times[selected]=(AirTime){player,now};
}
static bool recent_air(void *player,uint64_t now) {
    if (now==UINT64_MAX) return false;
    for (size_t i=0;i<1024;++i) if (air_times[i].player==player)
        return now>=air_times[i].time && now-air_times[i].time<=67;
    return false;
}
static void friction(void *local,void *movement) {
    if (!fast_jump()) { original_friction(local,movement);return; }
    reset_local_state();void *previous=friction_movement;friction_movement=movement;
    if (engine_airborne(movement,local)) remember_air(read_pointer(movement),clock_ms());
    original_friction(local,movement);friction_movement=previous;
}
static float friction_scale_for(uintptr_t caller) {
    if (!fast_jump() || caller!=base+T7O_MAC_FRICTION_SCALE_RETURN) return original_friction_scale();
    reset_local_state();uint8_t *ps=friction_movement ? read_pointer(friction_movement) : NULL;
    if (!ps || (ps[0x10]&0x80)) return original_friction_scale();
    float velocity[2];memcpy(velocity,ps+0x3c,sizeof(velocity));
    float square=velocity[0]*velocity[0]+velocity[1]*velocity[1];
    if (!(square>=10000.0f) || !recent_air(ps,clock_ms())) return original_friction_scale();
    return 0.0f;
}
static float friction_scale(void) { return friction_scale_for((uintptr_t)__builtin_return_address(0)); }
static void accelerate_for(void *player,void *local,const float *direction,float speed,float acceleration,uintptr_t caller) {
    if (fast_jump() && caller==base+T7O_MAC_AIR_ACCELERATE_RETURN) {
        uint8_t *ps=player;float velocity[2];memcpy(velocity,ps+0x3c,sizeof(velocity));
        if (!(ps[0x10]&0x80) && sqrtf(velocity[0]*velocity[0]+velocity[1]*velocity[1])<150.0f) {
            speed+=170.0f;acceleration*=4.0f;
        }
    }
    engine_accelerate(player,local,direction,speed,acceleration);
}
static void accelerate(void *player,void *local,const float *direction,float speed,float acceleration) {
    accelerate_for(player,local,direction,speed,acceleration,(uintptr_t)__builtin_return_address(0));
}
static bool jump_slowdown(void) { return enabled() && patches[T7O_JUMP_SLOWDOWN]; }
static float jump_penalty(void *player) {
    if (!jump_slowdown()) return original_jump_penalty(player);
    uint8_t *ps=player;
    if ((int32_t)read32(ps+0x24)>1000) { ps[0x12]&=(uint8_t)~4u;memset(ps+0x114,0,4); }
    return 1.0f;
}
static void apply_penalty(void *player) { if (!jump_slowdown()) original_apply_penalty(player); }
static int land_severity(void *player,float amount) {
    return jump_slowdown() ? 0 : original_land_severity(player,amount);
}
static unsigned char jump(void *movement,void *local,float height) {
    bool previous=jump_crouched;jump_crouched=(*(const uint8_t *)((uintptr_t)read_pointer(movement)+0x10)&0x80)!=0;
    ++jump_depth;unsigned char result=original_jump(movement,local,height);--jump_depth;
    jump_crouched=previous;return result;
}
void t7o_inline_dispatch(T7oInlineContext *context,unsigned operation) {
    if (!jump_slowdown()) return;
    if (operation==T7O_INLINE_JUMP_CAP) {
        if (jump_depth && !jump_crouched) context->flags|=1; /* original JBE skips the cap */
        return;
    }
    reset_local_state();uintptr_t stack=(uintptr_t)context+528;
    if (operation==T7O_INLINE_LAND_BEGIN) {
        uint8_t *ps=read_pointer((const void *)(uintptr_t)context->r12);
        if (!ps) return;
        for (size_t i=0;i<16;++i) if (!landings[i].player || landings[i].stack==stack) {
            landings[i].player=ps;landings[i].stack=stack;memcpy(landings[i].horizontal,ps+0x3c,8);return;
        }
    } else if (operation==T7O_INLINE_LAND_END) {
        for (size_t i=0;i<16;++i) if (landings[i].player && landings[i].stack==stack &&
            landings[i].player==(void *)(uintptr_t)context->r15) {
            uint8_t *ps=landings[i].player;memcpy(ps+0x3c,landings[i].horizontal,8);memset(ps+0x44,0,4);
            landings[i].player=NULL;return;
        }
    }
}
static bool slide_eligible(void *movement) {
    if (!enabled() || !patches[T7O_INSTANT_SLIDE]) return original_slide_eligible(movement);
    uint8_t *ps=read_pointer(movement);float velocity[2],yaw;memcpy(velocity,ps+0x3c,8);memcpy(&yaw,ps+0x31c,4);
    float magnitude=sqrtf(velocity[0]*velocity[0]+velocity[1]*velocity[1]);
    float reciprocal=1.0f/(magnitude<=0.0f ? 1.0f : magnitude);
    float direction[2]={velocity[0]*reciprocal,velocity[1]*reciprocal};
    return fabsf(engine_angle_delta(engine_vector_yaw(direction),yaw))<=180.0f;
}
static void clamp_pitch(void *player,void *command,int limit) {
    if (!(enabled() && patches[T7O_SLIDE_ANGLES] && limit==20)) original_clamp_pitch(player,command,limit);
}
static void force_view_for(void *movement,int time,int axis,int type,int flags,float amount,uintptr_t caller) {
    if (!(enabled() && patches[T7O_SLIDE_ANGLES] && caller==base+T7O_MAC_SLIDE_FORCE_VIEW_RETURN))
        original_force_view(movement,time,axis,type,flags,amount);
}
static void force_view(void *movement,int time,int axis,int type,int flags,float amount) {
    force_view_for(movement,time,axis,type,flags,amount,(uintptr_t)__builtin_return_address(0));
}
static void accelerate_bridge(void *context,const float direction[3],float speed,float acceleration) {
    AccelerationContext *c=context;engine_accelerate(c->player,c->local,direction,speed,acceleration);
}
static void slide_accelerate(void *player,void *local) {
    if (!enabled() || !patches[T7O_SLIDE_MOMENTUM]) { original_slide(player,local);return; }
    uint8_t *ps=player,*pml=local;uint64_t flags;memcpy(&flags,ps+0x10,8);
    T7oMovementLocal input={{0},0,0};
    memcpy(input.direction,pml+0x40,8);memcpy(&input.flags,pml+0x54,4);memcpy(&input.contact,pml+0x64,4);
    AccelerationContext context={player,local};
    t7o_slide_momentum((float *)(ps+0x3c),flags,&input,accelerate_bridge,&context);
}
static void update_sprint(void *movement,void *local) {
    int movement_enabled=enabled();
    if (movement_enabled!=last_enabled) {
        for (int i=0;i<T7O_MOVEMENT_COUNT;++i)
            t7o_mac_movement_set(t7o_movement_name((T7oMovementPatch)i),patches[i]);
        prepatch_set_native_value(T7O_MAC_JUMP_PENALTY_BRANCH,1,movement_enabled!=0);
        last_enabled=movement_enabled;
    }
    if (enabled()) {
        if (patches[T7O_FAST_ADS]) prepatch_set_native_value(T7O_MAC_ADS_SCALE,ads_multiplier(),true);
        const char *all=setting("movement_patch_all");int current=all && strtol(all,NULL,10)!=0;
        if (current!=last_all && t7o_mac_movement_set_all(current!=0)) last_all=current;
    }
    const char *value=enabled() ? setting("t7o_omnimovement") : NULL;
    if (!value || strtol(value,NULL,10)!=1) { original_sprint(movement,local);return; }
    uint8_t *bytes=movement;
    int8_t forward,right,adjusted_forward;
    uint32_t buttons,adjusted_buttons;
    /* Independently matched in the Mac sprint policy and its movement caller:
     * pmove holds PS* at +0, current command time +8, buttons +0xc and axes
     * +0x40/+0x41. This pointer is not a standalone usercmd structure. */
    memcpy(&forward,bytes+0x40,1);memcpy(&right,bytes+0x41,1);
    memcpy(&buttons,bytes+0xc,4);
    t7o_omni_sprint(forward,right,buttons,&adjusted_forward,&adjusted_buttons);
    memcpy(bytes+0x40,&adjusted_forward,1);memcpy(bytes+0xc,&adjusted_buttons,4);
    original_sprint(movement,local);
    memcpy(bytes+0x40,&forward,1);memcpy(bytes+0xc,&buttons,4);
}
bool t7o_mac_movement_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t strafe[]={0x55,0x48,0x89,0xe5,0xf3,0x0f,0x10,0x05,0x3b,0xb3,0x82,0x00};
    const uint8_t sprint[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    if (memcmp(prepatch_original_code(address(T7O_MAC_STRAFE_SCALE),sizeof(strafe)),strafe,sizeof(strafe)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_UPDATE_SPRINT),sizeof(sprint)),sprint,sizeof(sprint)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_SLIDE_ACCELERATE),sizeof(sprint)),sprint,sizeof(sprint))) return false;
    const uint32_t sites[]={T7O_MAC_SPRINT_LEAP_COMPARE_A,T7O_MAC_SPRINT_LEAP_COMPARE_B,
        T7O_MAC_SPRINT_LERP_COMPARE,T7O_MAC_JUMP_INTERVAL_COMPARE,T7O_MAC_SLIDE_SLOW_BRANCH,T7O_MAC_SLIDE_SPRINT_BRANCH,
        T7O_MAC_ADS_MODE_BRANCH_A,T7O_MAC_ADS_MODE_BRANCH_B,T7O_MAC_ADS_SCALE,
        T7O_MAC_SLIDE_FRICTION_LOAD,T7O_MAC_SWAP_TIMING_A,T7O_MAC_SWAP_TIMING_B,T7O_MAC_SWAP_ANIMATION,
        T7O_MAC_SWAP_PRESENTATION,T7O_MAC_JUMP_CAP_BRANCH,T7O_MAC_JUMP_PENALTY_BRANCH,T7O_MAC_LAND_BEGIN,T7O_MAC_LAND_END};
    for (size_t i=0;i<sizeof(sites)/sizeof(*sites);++i) if (!prepatch_native_site_present(sites[i])) return false;
    dvar_find=(void *)address(T7O_MAC_DVAR_FIND_NAME);dvar_string=(void *)address(T7O_MAC_DVAR_VALUE_STRING);
    dvar_effective=(void *)address(T7O_MAC_DVAR_EFFECTIVE);
    command_add=(void *)address(T7O_MAC_COMMAND_ADD);command_find=(void *)address(T7O_MAC_COMMAND_FIND);
    thread_context=(void *)address(T7O_MAC_THREAD_CONTEXT);engine_airborne=(void *)address(T7O_MAC_AIRBORNE);
    engine_vector_yaw=(void *)address(T7O_MAC_VECTOR_YAW);engine_angle_delta=(void *)address(T7O_MAC_ANGLE_DELTA);
    if (DobbyHook(address(T7O_MAC_STRAFE_SCALE),(void *)strafe_scale,(void **)&original_strafe)!=0 ||
        DobbyHook(address(T7O_MAC_UPDATE_SPRINT),(void *)update_sprint,(void **)&original_sprint)!=0 ||
        DobbyHook(address(T7O_MAC_SLIDE_ACCELERATE),(void *)slide_accelerate,(void **)&original_slide)!=0 ||
        DobbyHook(address(T7O_MAC_ACCELERATE),(void *)accelerate,(void **)&engine_accelerate)!=0 ||
        DobbyHook(address(T7O_MAC_FRICTION),(void *)friction,(void **)&original_friction)!=0 ||
        DobbyHook(address(T7O_MAC_FRICTION_SCALE),(void *)friction_scale,(void **)&original_friction_scale)!=0 ||
        DobbyHook(address(T7O_MAC_JUMP_PENALTY),(void *)jump_penalty,(void **)&original_jump_penalty)!=0 ||
        DobbyHook(address(T7O_MAC_APPLY_JUMP_PENALTY),(void *)apply_penalty,(void **)&original_apply_penalty)!=0 ||
        DobbyHook(address(T7O_MAC_JUMP),(void *)jump,(void **)&original_jump)!=0 ||
        DobbyHook(address(T7O_MAC_LAND_SEVERITY),(void *)land_severity,(void **)&original_land_severity)!=0 ||
        DobbyHook(address(T7O_MAC_SLIDE_ELIGIBLE),(void *)slide_eligible,(void **)&original_slide_eligible)!=0 ||
        DobbyHook(address(T7O_MAC_CLAMP_PITCH),(void *)clamp_pitch,(void **)&original_clamp_pitch)!=0 ||
        DobbyHook(address(T7O_MAC_FORCE_VIEW),(void *)force_view,(void **)&original_force_view)!=0) return false;
    installed=true;return true;
}
bool t7o_mac_movement_activate(void) {
    if (!installed) return false;
    if (!commands_registered) {
        if (command_find("movement_patch") || command_find("movement_patch_all")) return false;
        command_add("movement_patch",command_patch,command_records[0]);
        command_add("movement_patch_all",command_all,command_records[1]);
        if (!command_find("dvarflag")) command_add("dvarflag",command_dvarflag,command_records[2]);
        commands_registered=true;
    }
    if (!t7o_mac_movement_set_all(false)) return false;
    atomic_fetch_add(&state_generation,1);
    if (!prepatch_set_inline_callback(T7O_MAC_JUMP_CAP_BRANCH,t7o_inline_jump_cap) ||
        !prepatch_set_inline_callback(T7O_MAC_LAND_BEGIN,t7o_inline_land_begin) ||
        !prepatch_set_inline_callback(T7O_MAC_LAND_END,t7o_inline_land_end) ||
        !prepatch_set_native_value(T7O_MAC_JUMP_PENALTY_BRANCH,1,true)) return false;
    const char *all=setting("movement_patch_all");last_all=all && strtol(all,NULL,10)!=0;
    atomic_store(&active,true);last_enabled=enabled();
    prepatch_set_native_value(T7O_MAC_JUMP_PENALTY_BRANCH,1,last_enabled!=0);
    return t7o_mac_movement_set_all(last_all!=0);
}
void t7o_mac_movement_deactivate(void) {
    atomic_store(&active,false);
    atomic_fetch_add(&state_generation,1);
    if (installed) t7o_mac_movement_set_all(false);
    if (installed) {
        prepatch_set_native_value(T7O_MAC_JUMP_PENALTY_BRANCH,1,false);
        prepatch_set_inline_callback(T7O_MAC_JUMP_CAP_BRANCH,NULL);
        prepatch_set_inline_callback(T7O_MAC_LAND_BEGIN,NULL);
        prepatch_set_inline_callback(T7O_MAC_LAND_END,NULL);
    }
}
