#include "t7o_mac_freeaim.h"
#include "t7o_freeaim.h"
#include "t7o_mac_offsets.h"
#include "t7o_inline.h"
#include "prepatch.h"
#include "dobby.h"
#include <math.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
enum { LOCALS=2,REMOTES=64 };
typedef struct { const void *cg;uint64_t seen,transition,sent_at;unsigned kind;bool sent;int point[3]; } Local;
typedef struct { void *client;uint64_t received;bool enabled;float point[3]; } Remote;
static uint64_t base,last_cleanup;
uintptr_t t7o_freeaim_reticle_resume;
_Static_assert(offsetof(T7oInlineContext,return_address)==392,"inline resume slot");
static bool installed;
static atomic_bool active;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Local locals[LOCALS];static Remote remotes[REMOTES];
static _Thread_local uint8_t *spread_pose;
typedef struct { uint8_t *parameters;uintptr_t frame;float original[3][3],replacement[3][3]; } TraceRestore;
static _Thread_local TraceRestore trace_restore;
static T7oAimSnapshot snapshots[LOCALS][32];static unsigned snapshot_cursor[LOCALS];
static float last_muzzle[LOCALS][3];static uint64_t muzzle_time[LOCALS];static unsigned flash_tag;
static void *(*client_dobj)(unsigned,unsigned);
static bool (*world_tag)(const void *,void *,unsigned,float *);
static unsigned (*intern_string)(const char *,unsigned,unsigned,unsigned);
/* Six GP arguments, five independent SSE arguments, then four stack GP
 * arguments on Mac. Windows assigns SSE registers by argument position. */
typedef void (*Spread)(void *,float,float,const float *,float *,float *,float,float,
    const float *,const float *,const float *,uintptr_t,int,int,float);
static Spread original_spread;
static uintptr_t (*original_projectile)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t (*original_grenade)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t (*original_rocket)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static void (*original_bullet)(void *,float,void *,void *,int);
static void (*original_command)(int,void *),(*original_execute)(void *,const char *,int,int);
static void *(*find_dvar)(const char *);
static const char *(*dvar_text)(const void *);
static void (*set_string)(const char *,const char *,bool),(*send_client)(int,const char *);
static bool (*server_running)(void);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t rva) { return T7O_MAC_ADDRESS(rva); }
static uint32_t u32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static void *pointer(const void *p) { void *v;memcpy(&v,p,8);return v; }
#ifndef T7O_AIM_MILLISECONDS
static uint64_t milliseconds(void) {
    struct timespec time;if (clock_gettime(CLOCK_MONOTONIC,&time)) return 0;
    return (uint64_t)time.tv_sec*1000+(uint64_t)time.tv_nsec/1000000;
}
#define T7O_AIM_MILLISECONDS milliseconds
#endif
static const char *setting(const char *name) {
    void *dvar=find_dvar(name);return dvar ? dvar_text(dvar) : NULL;
}
static const char *client_setting(const char *name,unsigned client) {
    char key[80];snprintf(key,sizeof(key),"%s_%u",name,client);
    const char *text=setting(key);return !text && !client ? setting(name) : text;
}
static bool enabled_value(const char *text,bool fallback) {
    if (!text) return fallback;
    const char *p=text;float value;
    if (!t7o_freeaim_number(&p,&value)) return fallback;
    while (*p==' ' || (*p>=9 && *p<=13)) ++p;
    return *p ? fallback : value!=0;
}
static bool zombies_map(void) {
    const char *name=setting("mapname");return name && !strncmp(name,"zm_",3);
}
static uint8_t *cg_for(unsigned client) {
    uint8_t *array=pointer(address(T7O_MAC_CG_ARRAY));
    return array && client<LOCALS && client<u32(address(T7O_MAC_CG_COUNT)) ? array+(size_t)client*T7O_MAC_CG_STRIDE : NULL;
}
void t7o_mac_freeaim_seen(const void *cg,uintptr_t caller) {
    if (!atomic_load(&active) || caller!=base+T7O_MAC_FREEAIM_SEEN_RETURN) return;
    for (unsigned c=0;c<LOCALS;++c) if (cg && cg==cg_for(c)) {
        uint64_t now=T7O_AIM_MILLISECONDS();pthread_mutex_lock(&lock);Local *local=&locals[c];
        if (local->cg!=cg) { local->kind=0;local->transition=0; }
        local->cg=cg;local->seen=now;pthread_mutex_unlock(&lock);break;
    }
}
static bool local_gate(unsigned client,uint64_t now) {
    uint8_t *cg=cg_for(client);bool fresh=false,stable=false;
    bool preference=zombies_map() && enabled_value(setting("aae_freeaim_enabled"),true) &&
        enabled_value(client_setting("aae_freeaim_enabled",client),true);
    pthread_mutex_lock(&lock);Local *local=&locals[client];
    if (cg && local->cg==cg && local->seen && now>=local->seen && now-local->seen<=250 && u32(cg)<REMOTES) fresh=true;
    unsigned kind=fresh && cg[0x11a8c9] ? 2:1;
    if (local->kind!=kind) { local->kind=kind;local->transition=now; }
    else stable=kind==2 && now>=local->transition && now-local->transition>=250;
    pthread_mutex_unlock(&lock);
    return fresh && stable && preference && u32(cg+T7O_MAC_CG_PLAYER_STATE+4)!=4;
}
static bool local_target(unsigned client,uint64_t now,float out[3]) {
    return client<LOCALS && local_gate(client,now) && t7o_freeaim_target(client_setting("aae_freeaim_target",client),out);
}
static bool finite_point(const float p[3]) {
    for (unsigned i=0;i<3;++i) if (!isfinite(p[i]) || fabsf(p[i])>=10000000) return false;
    return true;
}
static void finish_trace(void) {
    TraceRestore *restore=&trace_restore;if (!restore->parameters) return;
    const unsigned offsets[]={0x10,0x1c,0x60};
    for (unsigned i=0;i<3;++i) {
        uint8_t *field=restore->parameters+offsets[i];
        if (!memcmp(field,restore->replacement[i],12)) memcpy(field,restore->original[i],12);
    }
    memset(restore,0,sizeof(*restore));
}
static void begin_trace(T7oInlineContext *context) {
    if (trace_restore.parameters || context->rsi>=16 || context->rsi>=u32(address(T7O_MAC_FREEAIM_TRACE_COUNT)) ||
        context->r13!=(uintptr_t)address(T7O_MAC_FREEAIM_TRACE_QUEUE)) return;
    uint8_t *entry=(uint8_t *)address(T7O_MAC_FREEAIM_TRACE_QUEUE)+(size_t)context->rsi*0x8b0;
    unsigned client=u32(entry),entity=u32(entry+0x58);if (client>=LOCALS || entity>=REMOTES) return;
    uint8_t *cg=cg_for(client);uint64_t now=T7O_AIM_MILLISECONDS();
    if (!cg || u32(cg)!=entity || !local_gate(client,now)) return;
    TraceRestore restore={.parameters=entry+4,.frame=context->rbp};
    memcpy(restore.original[0],entry+0x14,12);memcpy(restore.original[1],entry+0x20,12);memcpy(restore.original[2],entry+0x64,12);
    pthread_mutex_lock(&lock);
    bool selected=t7o_freeaim_trace_select(snapshots[client],snapshot_cursor[client],now,
        restore.original[0],restore.original[1],restore.original[2],restore.replacement[0],restore.replacement[1]);
    pthread_mutex_unlock(&lock);if (!selected) return;
    memcpy(restore.replacement[2],restore.replacement[0],12);trace_restore=restore;
    memcpy(entry+0x14,restore.replacement[0],12);memcpy(entry+0x20,restore.replacement[1],12);memcpy(entry+0x64,restore.replacement[2],12);
}
static void record_snapshot(unsigned client,const uint8_t *pose,const float origin[3],const float target[3]) {
    uint64_t now=T7O_AIM_MILLISECONDS();float muzzle[3]={NAN,NAN,NAN};bool fresh=false;
    if (client_dobj && world_tag && flash_tag) {
        void *object=client_dobj(u32(pose+0x3f8),client);
        fresh=object && world_tag(pose,object,flash_tag,muzzle) && finite_point(muzzle);
    }
    pthread_mutex_lock(&lock);
    if (!fresh) {
        if (muzzle_time[client] && now>=muzzle_time[client] && now-muzzle_time[client]<=250) memcpy(muzzle,last_muzzle[client],12);
        else for (unsigned i=0;i<3;++i) muzzle[i]=NAN;
    }
    float x=origin[0]-muzzle[0],y=origin[1]-muzzle[1],z=origin[2]-muzzle[2];
    if (finite_point(origin) && finite_point(target) && finite_point(muzzle) && z*z+y*y+x*x+0.0f<=16384) {
        if (fresh) { memcpy(last_muzzle[client],muzzle,12);muzzle_time[client]=now; }
        T7oAimSnapshot *snapshot=&snapshots[client][snapshot_cursor[client]++&31];
        *snapshot=(T7oAimSnapshot){.time=now};memcpy(snapshot->origin,origin,12);memcpy(snapshot->muzzle,muzzle,12);
        memcpy(snapshot->target,target,12);memcpy(snapshot->target_copy,target,12);
    }
    pthread_mutex_unlock(&lock);
}
bool t7o_mac_freeaim_crosshair(const void *cg,uintptr_t caller,bool original) {
    if (original || !atomic_load(&active) || caller!=base+T7O_MAC_FREEAIM_SEEN_RETURN) return original;
    for (unsigned c=0;c<LOCALS;++c) if (cg && cg==cg_for(c)) return local_gate(c,T7O_AIM_MILLISECONDS());
    return original;
}
void t7o_mac_freeaim_inline_dispatch(T7oInlineContext *context,unsigned operation) {
    if (operation==T7O_INLINE_FREEAIM_TRACE_END) {
        if (context && trace_restore.parameters && context->rbp==trace_restore.frame) finish_trace();
        return;
    }
    if (!atomic_load(&active)) return;
    if (operation==T7O_INLINE_FREEAIM_TRACE_BEGIN) { if (context) begin_trace(context);return; }
    if (operation==T7O_INLINE_FREEAIM_SPREAD) {
        spread_pose=pointer((const void *)(uintptr_t)(context->rbp-0x58));return;
    }
    const uint8_t *cg=(const void *)(uintptr_t)context->r13;unsigned client=LOCALS;
    for (unsigned c=0;c<LOCALS;++c) if (cg && cg==cg_for(c)) { client=c;break; }
    if (client==LOCALS || !local_gate(client,T7O_AIM_MILLISECONDS())) return;
    if (operation==T7O_INLINE_FREEAIM_RETICLE) {
        /* Clang inlined the Windows predicate. Only bypass that predicate's
         * block, after the shared alpha/material/ADS checks, in this draw. */
        context->return_address=(uintptr_t)t7o_inline_freeaim_reticle_skip;
    } else if (operation==T7O_INLINE_FREEAIM_ADS) {
        float zero=0;memcpy((void *)(uintptr_t)(context->rbp-0x50),&zero,4);
    } else if (operation==T7O_INLINE_FREEAIM_FADE) {
        float fraction,value;memcpy(&fraction,cg+0x11abcc,4);memcpy(&value,context->xmm[1],4);
        const uint8_t *weapon=(const void *)(uintptr_t)context->r12;
        if (!weapon) return;
        float minimum=(float)(int32_t)u32(weapon+0x6f0);
        if (isfinite(fraction) && isfinite(value) && minimum>=0) {
            fraction=fminf(1,fmaxf(0,fraction));value=(minimum-value)*fraction+value;memcpy(context->xmm[1],&value,4);
        }
    }
}
static void spread(void *seed,float fov_x,float fov_y,const float *origin,
    float *end,float *direction,float minimum,float maximum,const float *forward,const float *right,
    const float *up,uintptr_t weapon,int index,int count,float distance) {
    const uint8_t *pose=spread_pose;spread_pose=NULL;
    float axis[9],rotated[9],target[3];bool changed=false;
    /* The one-use TLS pose is produced only by the two verified CALL sites,
     * immediately before this entry. Their replayed CALL returns into the
     * prepatch stub, so an original return-address comparison would fail. */
    if (atomic_load(&active) && pose && origin && forward && right && up) {
        unsigned entity=u32(pose+0x3f8);
        for (unsigned client=0;client<LOCALS;++client) {
            uint8_t *cg=cg_for(client);
            if (cg && u32(cg)==entity && local_target(client,T7O_AIM_MILLISECONDS(),target)) {
                memcpy(axis,forward,12);memcpy(axis+3,right,12);memcpy(axis+6,up,12);
                changed=t7o_freeaim_rotate(origin,axis,target,rotated);
                if (changed) record_snapshot(client,pose,origin,target);
                break;
            }
        }
    }
    original_spread(seed,fov_x,fov_y,origin,end,direction,minimum,maximum,
        changed ? rotated:forward,changed ? rotated+3:right,changed ? rotated+6:up,weapon,index,count,distance);
}
static unsigned client_index(void *client) {
    uintptr_t first=(uintptr_t)pointer(address(T7O_MAC_SV_CLIENTS)),p=(uintptr_t)client;
    unsigned count=u32(address(T7O_MAC_SV_CLIENT_COUNT));
    if (!first || !p || p<first || count>REMOTES || (p-first)%T7O_MAC_SV_CLIENT_STRIDE) return REMOTES;
    uintptr_t index=(p-first)/T7O_MAC_SV_CLIENT_STRIDE;
    return index<count && index<REMOTES ? (unsigned)index : REMOTES;
}
static bool remote_target(unsigned entity,float out[3]) {
    if (entity>=REMOTES) return false;
    Remote snapshot;pthread_mutex_lock(&lock);snapshot=remotes[entity];pthread_mutex_unlock(&lock);
    if (!snapshot.enabled || !snapshot.received || client_index(snapshot.client)!=entity || u32(snapshot.client)!=5) return false;
    memcpy(out,snapshot.point,12);return true;
}
static bool actor_target(void *actor,float out[3]) {
    if (!atomic_load(&active) || !actor || !zombies_map() || !enabled_value(setting("aae_freeaim_enabled"),true)) return false;
    uint8_t *client=pointer((uint8_t *)actor+0x250);unsigned entity=u32(actor);
    if (!client || !client[0xbc] || entity>=REMOTES) return false;
    if (remote_target(entity,out)) return true;
    uint64_t now=T7O_AIM_MILLISECONDS();
    for (unsigned c=0;c<LOCALS;++c) { uint8_t *cg=cg_for(c);if (cg && u32(cg)==entity && local_target(c,now,out)) return true; }
    return false;
}
static void execute(void *client,const char *text,int allowed,int old_server) {
    if (atomic_load(&active) && text && !strncmp(text,"aae ",4) && !old_server && zombies_map()) {
        unsigned index=client_index(client);
        if (index<REMOTES && u32(client)==5) {
            float target[3];T7oAimCommand command=t7o_freeaim_command(text,target);
            if (command!=T7O_AIM_INVALID) {
                Remote value={0};
                if (command==T7O_AIM_TARGET) {
                    value.client=client;value.enabled=true;value.received=T7O_AIM_MILLISECONDS();memcpy(value.point,target,12);
                }
                pthread_mutex_lock(&lock);remotes[index]=value;pthread_mutex_unlock(&lock);
            }
            return; /* The native handler consumes malformed aae commands too. */
        }
    }
    original_execute(client,text,allowed,old_server);
}
static void cleanup(uint64_t now) {
    pthread_mutex_lock(&lock);
    if (!last_cleanup || now<last_cleanup || now-last_cleanup>=500) {
        last_cleanup=now;
        for (unsigned i=0;i<REMOTES;++i)
            if (remotes[i].enabled && (client_index(remotes[i].client)!=i || u32(remotes[i].client)!=5)) memset(&remotes[i],0,sizeof(remotes[i]));
    }
    pthread_mutex_unlock(&lock);
}
static void create_command(int client,void *command) {
    original_command(client,command);
    if (!atomic_load(&active) || client<0 || client>=LOCALS) return;
    uint64_t now=T7O_AIM_MILLISECONDS();cleanup(now);
    float target[3];bool valid=local_target((unsigned)client,now,target);
    if (server_running()) return;
    int point[3]={0};
    if (valid) for (unsigned i=0;i<3;++i) { float half=target[i]*0.5f;point[i]=(int)(half+(half<0 ? -0.5f:0.5f)); }
    uint8_t *connections=pointer(address(T7O_MAC_CLIENT_CONNECTIONS));
    if (!connections) return;
    uint8_t *connection=connections+(size_t)client*0x25780;
    if (u32(connection+0x130)-u32(connection+0x134)>=16) return;
    char message[96];bool send=false;
    pthread_mutex_lock(&lock);Local *local=&locals[client];
    if (now>=local->sent_at && now-local->sent_at>=50) {
        if (valid && (!local->sent || memcmp(local->point,point,sizeof(point)))) {
            snprintf(message,sizeof(message),"aae %d %d %d",point[0],point[1],point[2]);send=true;
        } else if (!valid && local->sent) { memcpy(message,"aae off",8);send=true; }
        if (send) { local->sent_at=now;local->sent=valid;memcpy(local->point,point,sizeof(point)); }
    }
    pthread_mutex_unlock(&lock);
    if (send) send_client(client,message);
}
static bool projectile_direction(void *actor,uint8_t *projectile) {
    if (!projectile || !atomic_load(&active) || !enabled_value(setting("aae_freeaim_nonbullet"),true)) return false;
    float target[3];if (!actor_target(actor,target)) return false;
    float velocity[3],origin[3],axis[9];memcpy(origin,projectile+0x1c,12);memcpy(velocity,projectile+0x28,12);
    float speed=sqrtf(velocity[0]*velocity[0]+velocity[1]*velocity[1]+velocity[2]*velocity[2]);
    if (!isfinite(speed) || speed<=0.0001f) return false;
    float inverse=1/speed;for (unsigned i=0;i<3;++i) axis[i]=velocity[i]*inverse;
    float up_x=fabsf(axis[2])<=0.99f ? 0:1,up_z=fabsf(axis[2])<=0.99f ? 1:0;
    axis[3]=axis[1]*up_z-axis[2]*0;axis[4]=axis[2]*up_x-axis[0]*up_z;axis[5]=axis[0]*0-axis[1]*up_x;
    float length=sqrtf(axis[4]*axis[4]+axis[3]*axis[3]+axis[5]*axis[5]);
    if (!isfinite(length) || length<0.000001f) return false;
    inverse=1/length;for (unsigned i=3;i<6;++i) axis[i]*=inverse;
    axis[6]=axis[4]*axis[2]-axis[5]*axis[1];axis[7]=axis[5]*axis[0]-axis[3]*axis[2];axis[8]=axis[3]*axis[1]-axis[4]*axis[0];
    float result[9];if (!t7o_freeaim_rotate(origin,axis,target,result)) return false;
    for (unsigned i=0;i<3;++i) velocity[i]=result[i]*speed;
    float angles[3]={-(float)atan2((double)result[2],(double)sqrtf(result[0]*result[0]+result[1]*result[1]))*57.295780181884766f,
        (float)atan2((double)result[1],(double)result[0])*57.295780181884766f,0};
    memcpy(projectile+0x28,velocity,12);memcpy(projectile+0x23c,angles,12);return true;
}
static uintptr_t projectile(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g) {
    uintptr_t result=original_projectile(a,b,c,d,e,f,g);projectile_direction((void *)a,(void *)result);return result;
}
static uintptr_t grenade(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g) {
    uintptr_t result=original_grenade(a,b,c,d,e,f,g);projectile_direction((void *)a,(void *)result);return result;
}
static uintptr_t rocket(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g,uintptr_t h) {
    uintptr_t result=original_rocket(a,b,c,d,e,f,g,h);projectile_direction((void *)a,(void *)result);return result;
}
static void bullet(void *actor,float spread,void *parameters,void *target_entity,int time) {
    float point[3],axis[9],saved[9];bool changed=false;
    if (parameters && actor_target(actor,point)) {
        memcpy(saved,parameters,sizeof(saved));float origin[3];memcpy(origin,(uint8_t *)parameters+0x24,12);
        if (t7o_freeaim_rotate(origin,saved,point,axis)) { memcpy(parameters,axis,sizeof(axis));changed=true; }
    }
    original_bullet(actor,spread,parameters,target_entity,time);
    if (changed) memcpy(parameters,saved,sizeof(saved));
}
void t7o_mac_freeaim_deactivate(void) {
    atomic_store(&active,false);
    finish_trace();
    if (installed) {
        prepatch_set_inline_callback(T7O_MAC_FREEAIM_RETICLE,NULL);prepatch_set_inline_callback(T7O_MAC_FREEAIM_ADS,NULL);
        prepatch_set_inline_callback(T7O_MAC_FREEAIM_FADE,NULL);
        prepatch_set_inline_callback(T7O_MAC_FREEAIM_SPREAD_CALL_A,NULL);prepatch_set_inline_callback(T7O_MAC_FREEAIM_SPREAD_CALL_B,NULL);
        prepatch_set_inline_callback(T7O_MAC_FREEAIM_TRACE_BEGIN,NULL);
        /* Keep the passive completion callback for a queue already in flight
         * on another thread. It only restores that thread's recorded fields. */
    }
    pthread_mutex_lock(&lock);
    memset(locals,0,sizeof(locals));memset(remotes,0,sizeof(remotes));last_cleanup=0;
    memset(snapshots,0,sizeof(snapshots));memset(snapshot_cursor,0,sizeof(snapshot_cursor));memset(muzzle_time,0,sizeof(muzzle_time));
    pthread_mutex_unlock(&lock);spread_pose=NULL;
}
bool t7o_mac_freeaim_activate(void) {
    if (!installed) return false;
    if (atomic_load(&active)) return true;
    if (!prepatch_set_inline_callback(T7O_MAC_FREEAIM_RETICLE,t7o_inline_freeaim_reticle) ||
        !prepatch_set_inline_callback(T7O_MAC_FREEAIM_ADS,t7o_inline_freeaim_ads) ||
        !prepatch_set_inline_callback(T7O_MAC_FREEAIM_FADE,t7o_inline_freeaim_fade) ||
        !prepatch_set_inline_callback(T7O_MAC_FREEAIM_SPREAD_CALL_A,t7o_inline_freeaim_spread) ||
        !prepatch_set_inline_callback(T7O_MAC_FREEAIM_SPREAD_CALL_B,t7o_inline_freeaim_spread) ||
        !prepatch_set_inline_callback(T7O_MAC_FREEAIM_TRACE_BEGIN,t7o_inline_freeaim_trace_begin) ||
        !prepatch_set_inline_callback(T7O_MAC_FREEAIM_TRACE_END,t7o_inline_freeaim_trace_end)) {
        t7o_mac_freeaim_deactivate();return false;
    }
    const char *names[]={"aae_freeaim_enabled","aae_freeaim_nonbullet","aae_freeaim_target","aae_freeaim_enabled_1","aae_freeaim_target_1"};
    for (unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) if (!find_dvar(names[i])) set_string(names[i],strstr(names[i],"target") ? "":"1",true);
    if (!flash_tag && intern_string) flash_tag=intern_string("tag_flash",0,10,3);
    atomic_store(&active,true);return true;
}
bool t7o_mac_freeaim_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint32_t functions[]={T7O_MAC_FREEAIM_PROJECTILE,T7O_MAC_FREEAIM_GRENADE,T7O_MAC_FREEAIM_ROCKET,T7O_MAC_FREEAIM_BULLET,
        T7O_MAC_CREATE_USERCMD,T7O_MAC_EXECUTE_CLIENT_COMMAND,T7O_MAC_SEND_CLIENT_COMMAND,T7O_MAC_FREEAIM_SPREAD,
        T7O_MAC_CLIENT_DOBJ,T7O_MAC_WORLD_TAG_POSITION};
    const uint8_t prologue[]={0x55,0x48,0x89,0xe5};
    for (unsigned i=0;i<sizeof(functions)/sizeof(functions[0]);++i)
        if (memcmp(prepatch_original_code(address(functions[i]),4),prologue,4)) return false;
    if (!prepatch_native_site_present(T7O_MAC_FREEAIM_RETICLE) || !prepatch_native_site_present(T7O_MAC_FREEAIM_ADS) ||
        !prepatch_native_site_present(T7O_MAC_FREEAIM_FADE) || !prepatch_native_site_present(T7O_MAC_FREEAIM_SPREAD_CALL_A) ||
        !prepatch_native_site_present(T7O_MAC_FREEAIM_SPREAD_CALL_B) || !prepatch_native_site_present(T7O_MAC_FREEAIM_TRACE_BEGIN) ||
        !prepatch_native_site_present(T7O_MAC_FREEAIM_TRACE_END)) return false;
    t7o_freeaim_reticle_resume=base+T7O_MAC_FREEAIM_RETICLE_RESUME;
    find_dvar=(void *)address(T7O_MAC_DVAR_FIND_NAME);dvar_text=(void *)address(T7O_MAC_DVAR_VALUE_STRING);
    set_string=(void *)address(T7O_MAC_SET_DVAR_STRING);send_client=(void *)address(T7O_MAC_SEND_CLIENT_COMMAND);server_running=(void *)address(T7O_MAC_SERVER_RUNNING);
    client_dobj=address(T7O_MAC_CLIENT_DOBJ);world_tag=address(T7O_MAC_WORLD_TAG_POSITION);intern_string=address(T7O_MAC_INTERN_STRING_SIZE);
    if (DobbyHook(address(T7O_MAC_FREEAIM_PROJECTILE),(void *)projectile,(void **)&original_projectile) ||
        DobbyHook(address(T7O_MAC_FREEAIM_GRENADE),(void *)grenade,(void **)&original_grenade) ||
        DobbyHook(address(T7O_MAC_FREEAIM_ROCKET),(void *)rocket,(void **)&original_rocket) ||
        DobbyHook(address(T7O_MAC_FREEAIM_BULLET),(void *)bullet,(void **)&original_bullet) ||
        DobbyHook(address(T7O_MAC_CREATE_USERCMD),(void *)create_command,(void **)&original_command) ||
        DobbyHook(address(T7O_MAC_EXECUTE_CLIENT_COMMAND),(void *)execute,(void **)&original_execute) ||
        DobbyHook(address(T7O_MAC_FREEAIM_SPREAD),(void *)spread,(void **)&original_spread)) return false;
    installed=true;return true;
}
