/* Input wrapper fixture. Mac addresses and live prediction are not exercised. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <math.h>
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_movement.c"
static const char *config="1",*omni="1",*scale="0.9",*all="0",*ads="1.4";
static uint8_t observed[128];static void *observed_local;static int calls;
typedef struct { uint32_t site,value;bool on; } SettingWrite;
static SettingWrite settings[32];static size_t setting_count;
static void *find_mock(const char *name) {
    if (!strcmp(name,"hook_movement_perks")) return &config;
    if (!strcmp(name,"t7o_omnimovement")) return &omni;
    if (!strcmp(name,"movement_patch_all")) return &all;
    if (!strcmp(name,"t7o_perk_fastads_multiplier")) return &ads;
    assert(!strcmp(name,"t7o_strafe_speed_scale"));return &scale;
}
static const char *string_mock(const void *variable) { return *(const char *const *)variable; }
static uint8_t flag_variable[0x90],flag_alias[0x90];
static void *flag_find(const char *name) {
    return !strcmp(name,"setting") ? flag_variable : !strcmp(name,"alias") ? flag_alias : NULL;
}
static void *effective_mock(void *variable) { assert(variable==flag_alias);return flag_variable; }
static void flag_commands(void) {
    void *(*previous)(const char *)=dvar_find; dvar_find=flag_find;dvar_effective=effective_mock;
    memset(flag_variable,0x5a,sizeof(flag_variable));uint32_t type=8;memcpy(flag_variable+0x1c,&type,4);
    uint8_t expected[sizeof(flag_variable)];memcpy(expected,flag_variable,sizeof(expected));
    const char *bits[]={"0","19","31"};uint32_t mask=UINT32_C(1)|(UINT32_C(1)<<19)|(UINT32_C(1)<<31);
    memcpy(expected+0x18,&mask,4);assert(set_dvar_flags("setting",3,bits));
    assert(!memcmp(flag_variable,expected,sizeof(expected)));
    assert(!set_dvar_flags("missing",1,bits));
    const char *bad[]={"not a flag"};assert(!set_dvar_flags("setting",1,bad));
    assert(!memcmp(flag_variable,expected,sizeof(expected)));
    type=15;memcpy(flag_alias+0x1c,&type,4);
    assert(set_dvar_flags("alias",0,NULL) && !read32(flag_variable+0x18));
    atomic_store(&active,false);assert(!set_dvar_flags("setting",3,bits));atomic_store(&active,true);
    dvar_find=previous;
}
static float strafe_mock(void) { return 0.42f; }
static void sprint_mock(void *movement,void *local) { memcpy(observed,movement,sizeof(observed));observed_local=local;++calls; }
const void *prepatch_original_code(const void *address,size_t size) { (void)address;(void)size;abort(); }
int BO3MacFix_Hook(void *address,void *replacement,void **original) { (void)address;(void)replacement;(void)original;abort(); }
bool prepatch_native_site_present(uint32_t site) { (void)site;return true; }
bool prepatch_set_native_value(uint32_t site,uint32_t value,bool on) {
    size_t i=0;while (i<setting_count && settings[i].site!=site) ++i;
    assert(i<32);if (i==setting_count) ++setting_count;
    settings[i]=(SettingWrite){site,value,on};return true;
}
static SettingWrite written(uint32_t site) {
    for (size_t i=0;i<setting_count;++i) if (settings[i].site==site) return settings[i];
    abort();
}
bool prepatch_set_inline_callback(uint32_t site,void (*callback)(void)) { (void)site;(void)callback;return true; }
void t7o_inline_jump_cap(void) {}
void t7o_inline_land_begin(void) {}
void t7o_inline_land_end(void) {}
static void *find_command(const char *name) { (void)name;return NULL; }
static void add_command(const char *name,void (*callback)(void),void *record) { assert(name && callback && record); }
static float acceleration_speed,acceleration_amount;
static void acceleration_mock(void *ps,void *local,const float *direction,float speed,float amount) {
    assert(ps && local && direction);acceleration_speed=speed;acceleration_amount=amount;
}
static float friction_mock_scale(void) { return 5.5f; }
static float penalty_mock(void *ps) { assert(ps);return 2.0f; }
static unsigned char jump_mock(void *pm,void *local,float height) {
    assert(pm && local && height==39.0f && jump_depth==1);
    T7oInlineContext frame={0};frame.flags=0x202;t7o_inline_dispatch(&frame,T7O_INLINE_JUMP_CAP);
    assert(((frame.flags&1)!=0)==!jump_crouched);return 7;
}
static void remaining_movement(void) {
    assert(t7o_mac_movement_set_all(true));
    for (int i=0;i<T7O_MOVEMENT_COUNT;++i) assert(patches[i]);
    assert(written(T7O_MAC_SPRINT_LEAP_COMPARE_A).value==500 && written(T7O_MAC_SPRINT_LERP_COMPARE).value==9);
    assert(written(T7O_MAC_SWAP_TIMING_A).value==0x3ee38866 && written(T7O_MAC_SWAP_PRESENTATION).on);
    assert(written(T7O_MAC_SLIDE_FRICTION_LOAD).value==0 && written(T7O_MAC_SLIDE_FRICTION_LOAD).on);
    assert(t7o_mac_movement_set("fastswap",false) && !written(T7O_MAC_SWAP_ANIMATION).on && patches[T7O_FAST_ADS]);
    assert(!t7o_mac_movement_set("unknown",true));
    uint8_t ps[0x1000]={0},pm[128]={0},local[128]={0};void *pointer=ps;memcpy(pm,&pointer,sizeof(pointer));
    float velocity[3]={30,40,17},direction[3]={1,0,0};memcpy(ps+0x3c,velocity,12);
    engine_accelerate=acceleration_mock;
    accelerate_for(ps,local,direction,200,1,base+T7O_MAC_AIR_ACCELERATE_RETURN);
    assert(acceleration_speed==370 && acceleration_amount==4);
    accelerate_for(ps,local,direction,200,1,base+T7O_MAC_AIR_ACCELERATE_RETURN+1);
    assert(acceleration_speed==200 && acceleration_amount==1);
    ps[0x10]=0x80;accelerate_for(ps,local,direction,200,1,base+T7O_MAC_AIR_ACCELERATE_RETURN);
    assert(acceleration_speed==200 && acceleration_amount==1);ps[0x10]=0;
    original_friction_scale=friction_mock_scale;reset_local_state();friction_movement=pm;
    velocity[0]=100;velocity[1]=0;memcpy(ps+0x3c,velocity,12);remember_air(ps,clock_ms());
    assert(friction_scale_for(base+T7O_MAC_FRICTION_SCALE_RETURN)==0);
    assert(friction_scale_for(base+T7O_MAC_FRICTION_SCALE_RETURN+1)==5.5f);
    assert(recent_air(ps,air_times[0].time+67) && !recent_air(ps,air_times[0].time+68));
    assert(!recent_air(local,clock_ms()));
    original_jump_penalty=penalty_mock;
    int32_t time=1001;memcpy(ps+0x24,&time,4);ps[0x12]=4;memset(ps+0x114,0xff,4);
    assert(jump_penalty(ps)==1 && !(ps[0x12]&4) && !read32(ps+0x114));
    original_jump=jump_mock;assert(jump(pm,local,39)==7 && !jump_depth);
    ps[0x10]=0x80;assert(jump(pm,local,39)==7 && !jump_depth);ps[0x10]=0;
    T7oInlineContext frame={0};frame.r12=(uintptr_t)pm;frame.r15=(uintptr_t)ps;
    memcpy(ps+0x3c,velocity,12);t7o_inline_dispatch(&frame,T7O_INLINE_LAND_BEGIN);
    memset(ps+0x3c,0xff,12);t7o_inline_dispatch(&frame,T7O_INLINE_LAND_END);
    assert(!memcmp(ps+0x3c,velocity,8) && read32(ps+0x44)==0);
    uint8_t movement[128]={0};config="0";update_sprint(movement,local);
    assert(!written(T7O_MAC_SPRINT_LEAP_COMPARE_A).on && patches[T7O_SPRINT_LEAP]);
    config="1";update_sprint(movement,local);assert(written(T7O_MAC_SPRINT_LEAP_COMPARE_A).on);
    t7o_mac_movement_deactivate();assert(!atomic_load(&active));
    for (int i=0;i<T7O_MOVEMENT_COUNT;++i) assert(!patches[i]);
}
int main(void) {
    dvar_find=find_mock;dvar_string=string_mock;original_strafe=strafe_mock;original_sprint=sprint_mock;
    command_find=find_command;command_add=add_command;
    assert(strafe_scale()==0.42f && !t7o_mac_movement_activate());
    installed=true;assert(t7o_mac_movement_activate());
    flag_commands();
    assert(strafe_scale()==0.9f);scale="nan";assert(strafe_scale()==0.667f);
    scale="2";assert(strafe_scale()==0.97f);
    scale="0.1";assert(strafe_scale()==0.667f);
    config="0";assert(strafe_scale()==0.42f);config="1";
    int axes[]={-128,-127,-40,0,40,127};
    for (unsigned i=0;i<6;++i) for (unsigned j=0;j<6;++j) for (int sprint=0;sprint<2;++sprint) {
        uint8_t before[128],movement[128],expected[128];memset(before,0xa5,sizeof(before));
        uint32_t buttons=0x80000001 | (sprint ? 1u<<18 : 0);
        int8_t f=(int8_t)axes[i],r=(int8_t)axes[j];
        memcpy(before+0xc,&buttons,4);memcpy(before+0x40,&f,1);memcpy(before+0x41,&r,1);
        memcpy(movement,before,sizeof(before));memcpy(expected,before,sizeof(before));
        int magnitude=abs(axes[i])>abs(axes[j]) ? abs(axes[i]) : abs(axes[j]);
        expected[0x40]=(uint8_t)(magnitude>127 ? 127 : magnitude);
        if (sprint && magnitude) { uint32_t adjusted=buttons | 1u<<30;memcpy(expected+0xc,&adjusted,4); }
        update_sprint(movement,&calls);
        assert(!memcmp(observed,expected,sizeof(expected)) && !memcmp(movement,before,sizeof(before)) && observed_local==&calls);
        omni="0";update_sprint(movement,&calls);assert(!memcmp(observed,before,sizeof(before)));omni="1";
    }
    assert(!t7o_mac_movement_install(0,false));
    remaining_movement();
    puts("Mac movement settings, acceleration call sites, landing/cap preservation, timer boundaries and reset passed (substituted engine)");
}
