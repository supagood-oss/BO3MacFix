#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define T7O_DEV_NATIVE 1
static void *test_address(uint32_t);
static uint64_t now=1000;
static uint64_t test_milliseconds(void) { return now; }
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_AIM_MILLISECONDS test_milliseconds
#include "../source/t7o_mac_freeaim.c"
static uint8_t cg_data[2*T7O_MAC_CG_STRIDE],connections[2*0x25780],clients[2*T7O_MAC_SV_CLIENT_STRIDE];
static uintptr_t cg_pointer=(uintptr_t)cg_data,connection_pointer=(uintptr_t)connections,client_pointer=(uintptr_t)clients;
static uint32_t cg_count=2,client_count=2;
static uint8_t trace_queue[16*0x8b0];static uint32_t trace_count=1;
static uint8_t actor[0x4f8],player[0x400],shot[0x4f8];
static const char *map="zm_test",*enabled="1",*nonbullet="1",*target0="100 200 300",*target1="0 100 0";
static bool host,changed_bullet;static unsigned commands,executed,sent,bullets,projectiles;static char message[96];
static bool changed_spread;static unsigned spreads;
static void *test_address(uint32_t rva) {
    switch (rva) {
    case T7O_MAC_CG_ARRAY:return &cg_pointer;
    case T7O_MAC_CG_COUNT:return &cg_count;
    case T7O_MAC_CLIENT_CONNECTIONS:return &connection_pointer;
    case T7O_MAC_SV_CLIENTS:return &client_pointer;
    case T7O_MAC_SV_CLIENT_COUNT:return &client_count;
    case T7O_MAC_FREEAIM_TRACE_QUEUE:return trace_queue;
    case T7O_MAC_FREEAIM_TRACE_COUNT:return &trace_count;
    default:abort();
    }
}
static void put32(void *p,uint32_t v) { memcpy(p,&v,4); }
static void put_pointer(void *p,const void *v) { memcpy(p,&v,8); }
static void *find_mock(const char *name) { return (void *)name; }
static const char *text_mock(const void *p) {
    const char *name=p;
    if (!strcmp(name,"mapname")) return map;
    if (!strcmp(name,"aae_freeaim_enabled") || !strncmp(name,"aae_freeaim_enabled_",20)) return enabled;
    if (!strcmp(name,"aae_freeaim_nonbullet")) return nonbullet;
    if (!strcmp(name,"aae_freeaim_target") || !strcmp(name,"aae_freeaim_target_0")) return target0;
    if (!strcmp(name,"aae_freeaim_target_1")) return target1;
    abort();
}
static void set_mock(const char *name,const char *value,bool force) { (void)name;(void)value;(void)force;abort(); }
static unsigned intern_mock(const char *name,unsigned user,unsigned length,unsigned type) {
    assert(!strcmp(name,"tag_flash") && !user && length==10 && type==3);return 99;
}
static bool muzzle_valid=true;
static void *dobj_mock(unsigned entity,unsigned client) { assert(entity==0 && client==0);return shot; }
static bool world_mock(const void *pose,void *object,unsigned tag,float *out) {
    assert(pose && object==shot && tag==99);memcpy(out,(float[3]){2,3,4},12);return muzzle_valid;
}
static bool host_mock(void) { return host; }
static void command_mock(int client,void *command) { assert(client==0 && command==shot);++commands; }
static void send_mock(int client,const char *text) { assert(client==0 && commands);++sent;snprintf(message,sizeof(message),"%s",text); }
static void execute_mock(void *client,const char *text,int allowed,int old) { assert(client && text && allowed==7 && old>=0);++executed; }
static void bullet_mock(void *owner,float spread,void *parameters,void *target,int time) {
    assert(owner==actor && spread==0.375f && target==(void *)0x8888 && time==2345);++bullets;
    const float *axis=parameters;
    if (changed_bullet) assert(axis[0]!=1 || axis[1]!=0 || axis[2]!=0);
    else assert(axis[0]==1 && axis[1]==0 && axis[2]==0);
    assert(axis[9]==0 && axis[10]==0 && axis[11]==0);
}
static uintptr_t projectile_mock(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g) {
    assert(a==(uintptr_t)actor && b==2 && c==3 && d==4 && e==5 && f==6 && g==7);++projectiles;return (uintptr_t)shot;
}
static uintptr_t rocket_mock(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g,uintptr_t h) {
    assert(h==8);return projectile_mock(a,b,c,d,e,f,g);
}
static void spread_mock(void *seed,float x,float y,const float *origin,float *end,float *direction,float minimum,float maximum,
    const float *forward,const float *right,const float *up,uintptr_t weapon,int index,int count,float distance) {
    assert(seed==shot && x==80 && y==60 && origin && end==(void *)0x1234 && direction==(void *)0x5678);
    assert(minimum==0.25f && maximum==0.75f && weapon==42 && index==3 && count==5 && distance==10000);++spreads;
    if (changed_spread) assert(fabsf(forward[0])<0.00001f && fabsf(forward[1]-1)<0.00001f);
    else assert(forward[0]==1 && forward[1]==0);
    assert(right && up);
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *f,void **o) { (void)p;(void)f;(void)o;abort(); }
bool prepatch_native_site_present(uint32_t site) { (void)site;return true; }
bool prepatch_set_inline_callback(uint32_t site,void (*callback)(void)) { (void)site;(void)callback;return true; }
void t7o_inline_freeaim_reticle(void) {}
void t7o_inline_freeaim_ads(void) {}
void t7o_inline_freeaim_fade(void) {}
void t7o_inline_freeaim_reticle_skip(void) {}
void t7o_inline_freeaim_spread(void) {}
void t7o_inline_freeaim_trace_begin(void) {}
void t7o_inline_freeaim_trace_end(void) {}
static void see(unsigned client) { t7o_mac_freeaim_seen(cg_data+(size_t)client*T7O_MAC_CG_STRIDE,base+T7O_MAC_FREEAIM_SEEN_RETURN); }
int main(void) {
    assert(sizeof(T7oAimSnapshot)==64 && offsetof(T7oAimSnapshot,time)==48 && offsetof(T7oAimSnapshot,consumed)==56);
    assert(!t7o_mac_freeaim_install(0,false) && !t7o_mac_freeaim_activate());
    base=0x100000000;installed=true;find_dvar=find_mock;dvar_text=text_mock;set_string=set_mock;server_running=host_mock;send_client=send_mock;
    original_command=command_mock;original_execute=execute_mock;original_bullet=bullet_mock;
    original_projectile=projectile_mock;original_grenade=projectile_mock;original_rocket=rocket_mock;
    original_spread=spread_mock;
    client_dobj=dobj_mock;world_tag=world_mock;intern_string=intern_mock;
    assert(t7o_mac_freeaim_activate());
    for (unsigned i=0;i<2;++i) { put32(cg_data+i*T7O_MAC_CG_STRIDE,i);cg_data[i*T7O_MAC_CG_STRIDE+0x11a8c9]=1;put32(clients+i*T7O_MAC_SV_CLIENT_STRIDE,5); }
    put_pointer(actor+0x250,player);player[0xbc]=1;put32(actor,0);
    assert(!local_gate(0,now));see(0);assert(!local_gate(0,now));now=1250;see(0);assert(local_gate(0,now));
    create_command(0,shot);assert(sent==1 && !strcmp(message,"aae 50 100 150"));
    assert(t7o_mac_freeaim_crosshair(cg_data,base+T7O_MAC_FREEAIM_SEEN_RETURN,false));
    assert(!t7o_mac_freeaim_crosshair(cg_data,0,false));
    T7oInlineContext frame={.r13=(uintptr_t)cg_data,.return_address=33};
    t7o_mac_freeaim_inline_dispatch(&frame,T7O_INLINE_FREEAIM_RETICLE);
    assert(frame.return_address==(uintptr_t)t7o_inline_freeaim_reticle_skip);
    float stack_fraction=0.75f;frame.rbp=(uintptr_t)&stack_fraction+0x50;
    t7o_mac_freeaim_inline_dispatch(&frame,T7O_INLINE_FREEAIM_ADS);assert(stack_fraction==0);
    uint8_t weapon[0xe10]={0};frame.r12=(uintptr_t)weapon;put32(weapon+0x6f0,2);
    float radius=10,fraction=0.5f;memcpy(frame.xmm[1],&radius,4);memcpy(cg_data+0x11abcc,&fraction,4);
    t7o_mac_freeaim_inline_dispatch(&frame,T7O_INLINE_FREEAIM_FADE);memcpy(&radius,frame.xmm[1],4);assert(radius==6);
    now=1260;target0="-1 -2 -3";see(0);create_command(0,shot);assert(sent==1);
    now=1300;see(0);create_command(0,shot);assert(sent==2 && !strcmp(message,"aae -1 -1 -2"));
    now=1350;see(0);create_command(0,shot);assert(sent==2); /* unchanged points do not fill the reliable queue */
    enabled="0";create_command(0,shot);assert(sent==3 && !strcmp(message,"aae off"));enabled="1";
    now=1400;see(0);put32(connections+0x130,16);put32(connections+0x134,0);create_command(0,shot);assert(sent==3);
    put32(connections+0x130,0);put32(connections+0x134,0xfffffffe);create_command(0,shot);assert(sent==4);
    host=true;now=1450;target0="0 100 0";see(0);create_command(0,shot);assert(sent==4);
    uint8_t pose[0x400]={0};uint8_t caller_frame[0x80]={0};put_pointer(caller_frame,pose);
    frame.rbp=(uintptr_t)caller_frame+0x58;t7o_mac_freeaim_inline_dispatch(&frame,T7O_INLINE_FREEAIM_SPREAD);
    assert(spread_pose==pose);changed_spread=true;
    const float basis[9]={1,0,0,0,-1,0,0,0,1},origin[3]={0};
    spread(shot,80,60,origin,(void *)0x1234,(void *)0x5678,0.25f,0.75f,basis,basis+3,basis+6,42,3,5,10000);
    assert(spreads==1 && !spread_pose && snapshot_cursor[0]==1);changed_spread=false;
    /* The Mac queue has inlined the Windows trace helper. Only the selected
     * vectors change; completion restores each field unless the engine edited
     * it, and a nested queue frame cannot restore the outer frame. */
    memcpy(trace_queue+0x14,origin,12);memcpy(trace_queue+0x20,(float[3]){100,0,0},12);
    memcpy(trace_queue+0x64,(float[3]){2,3,4},12);uint8_t before[0x8b0];memcpy(before,trace_queue,sizeof(before));
    T7oInlineContext trace={.rsi=0,.r13=(uintptr_t)trace_queue,.rbp=0x9999};
    t7o_mac_freeaim_inline_dispatch(&trace,T7O_INLINE_FREEAIM_TRACE_BEGIN);
    assert(trace_restore.parameters==trace_queue+4 && snapshots[0][0].consumed);
    assert(!memcmp(trace_queue+0x14,(float[3]){2,3,4},12) && !memcmp(trace_queue+0x20,(float[3]){0,100,0},12));
    T7oInlineContext nested=trace;nested.rbp=0x8888;
    t7o_mac_freeaim_inline_dispatch(&nested,T7O_INLINE_FREEAIM_TRACE_END);assert(trace_restore.parameters);
    t7o_mac_freeaim_inline_dispatch(&trace,T7O_INLINE_FREEAIM_TRACE_END);assert(!trace_restore.parameters && !memcmp(trace_queue,before,sizeof(before)));
    memcpy(&snapshots[0][1],&snapshots[0][0],sizeof(T7oAimSnapshot));snapshots[0][1].consumed=0;snapshot_cursor[0]=2;
    t7o_mac_freeaim_inline_dispatch(&trace,T7O_INLINE_FREEAIM_TRACE_BEGIN);trace_queue[0x20]=7;
    t7o_mac_freeaim_inline_dispatch(&trace,T7O_INLINE_FREEAIM_TRACE_END);assert(trace_queue[0x20]==7 && !trace_restore.parameters);
    muzzle_valid=false;now+=20;record_snapshot(0,pose,origin,(float[3]){0,200,0});assert(snapshot_cursor[0]==3);
    now+=251;record_snapshot(0,pose,origin,(float[3]){0,200,0});assert(snapshot_cursor[0]==3);now=1450;
    spread(shot,80,60,origin,(void *)0x1234,(void *)0x5678,0.25f,0.75f,basis,basis+3,basis+6,42,3,5,10000);
    assert(spreads==2 && basis[0]==1 && basis[1]==0);
    float parameters[12]={1,0,0,0,-1,0,0,0,1,0,0,0},saved[12];memcpy(saved,parameters,sizeof(saved));
    changed_bullet=true;bullet(actor,0.375f,parameters,(void *)0x8888,2345);assert(bullets==1 && !memcmp(saved,parameters,sizeof(saved)));
    float velocity[3]={10,0,0};memcpy(shot+0x28,velocity,12);assert(projectile((uintptr_t)actor,2,3,4,5,6,7)==(uintptr_t)shot);
    memcpy(velocity,shot+0x28,12);assert(fabsf(velocity[0])<0.00001f && fabsf(velocity[1]-10)<0.00001f);
    memcpy(shot+0x28,(float[3]){10,0,0},12);assert(grenade((uintptr_t)actor,2,3,4,5,6,7)==(uintptr_t)shot);
    memcpy(shot+0x28,(float[3]){10,0,0},12);assert(rocket((uintptr_t)actor,2,3,4,5,6,7,8)==(uintptr_t)shot && projectiles==3);
    nonbullet="0";memcpy(shot+0x28,(float[3]){10,0,0},12);assert(!projectile_direction(actor,shot));nonbullet="1";
    void *remote=clients+T7O_MAC_SV_CLIENT_STRIDE;
    execute(remote,"aae 10 0 0",7,0);assert(!executed && remotes[1].enabled && remotes[1].point[0]==20);
    execute(remote,"aae invalid",7,0);assert(!executed && remotes[1].enabled);
    execute(remote,"aae 20 0 0",7,1);assert(executed==1 && remotes[1].point[0]==20);
    put32(actor,1);float point[3];assert(actor_target(actor,point) && point[0]==20);
    execute(remote,"aae off",7,0);assert(!remotes[1].enabled);
    execute(remote,"ordinary command",7,0);assert(executed==2);
    execute(remote,"aae 1 2 3",7,0);put32(remote,0);now+=500;cleanup(now);assert(!remotes[1].enabled);put32(remote,5);
    put32(actor,0);now+=1000;assert(!local_gate(0,now));changed_bullet=false;
    bullet(actor,0.375f,parameters,(void *)0x8888,2345);assert(bullets==2);
    t7o_mac_freeaim_deactivate();assert(!locals[0].seen && !remotes[1].enabled);
    bullet(actor,0.375f,parameters,(void *)0x8888,2345);assert(bullets==3);
    puts("Mac free aim: view gates, reliable targets, shooting/spread, muzzle fallback, queue trace selection/restoration, nested frames, engine-edit preservation and unload passed (substituted engine)");
}
