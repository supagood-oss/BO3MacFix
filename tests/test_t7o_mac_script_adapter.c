/* Substituted-engine integration checks for the Mac adapter. Real routine
 * machine-code checks live in verify_mac_contracts.py; neither test qualifies
 * the game thread boundary, Rosetta, live VM lifetimes or co-op. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../source/t7o_mac_offsets.h"
static void *fixture_address(uint32_t offset);
#define T7O_MAC_ADDRESS(offset) fixture_address(offset)
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_script.c"

static uint8_t pubs[2*T7O_MAC_VM_INSTANCE_STRIDE],entities[4*T7O_MAC_SERVER_ENTITY_STRIDE];
static uint8_t anim_clients[18*T7O_MAC_SERVER_ANIM_CLIENT_STRIDE],definitions[0xa0004];
static uint8_t *cg_array;static int cg_count=2;
static void *animation_context=definitions,*tls_slot=&animation_context,*clients_slot=anim_clients;
static Builtin builtin;
static int original_calls,errors,weapon_calls,body_calls,event_calls;
static bool dual;static void *observed_ps;static unsigned observed_hand;static int observed_anim;
static int32_t pushed_int;static float pushed_vector[3];
static void *resolve_tls(void *descriptor);
static void *(*tls_resolver)(void *)=resolve_tls;
static void *fixture_address(uint32_t offset) {
    switch(offset) {
    case T7O_MAC_VM_PUBLIC:return pubs;
    case T7O_MAC_CG_COUNT:return &cg_count;
    case T7O_MAC_CG_ARRAY:return &cg_array;
    case T7O_MAC_PROFILE_BUILTIN:return &builtin;
    case T7O_MAC_PROFILE_FUNCTION:return original_profile;
    case T7O_MAC_SERVER_ENTITIES:return entities;
    case T7O_MAC_SERVER_ANIM_CLIENTS_GOT:return &clients_slot;
    case T7O_MAC_ANIMATION_TLS_DESCRIPTOR:return &tls_resolver;
    default:abort();
    }
}
static void *resolve_tls(void *descriptor) { assert(descriptor==&tls_resolver);return &tls_slot; }
static void write32(void *p,uint32_t value) { memcpy(p,&value,4); }
static void write_pointer(void *p,void *value) { memcpy(p,&value,sizeof(value)); }
static int32_t get_integer(unsigned instance,unsigned index) { return (int32_t)argument(instance,index)->value; }
static const char *get_text(unsigned instance,unsigned index) { return (const char *)(uintptr_t)argument(instance,index)->value; }
static void result_integer(unsigned instance,int32_t value) { assert(instance<2);pushed_int=value; }
static void result_vector(unsigned instance,const float *value) { assert(instance<2);memcpy(pushed_vector,value,sizeof(pushed_vector)); }
static void original(unsigned instance) { assert(instance<2);++original_calls; }
static void error_mock(unsigned instance,const char *message,bool terminal) { assert(instance<2 && message && !terminal);++errors; }
static void view_mock(const void *ps,float *result,int time) {
    assert(ps==cg_array+T7O_MAC_CG_STRIDE+T7O_MAC_CG_PLAYER_STATE && time==5678);
    result[0]=1;result[1]=2;result[2]=3;
}
static bool dual_mock(uint64_t weapon) { assert(weapon==123);return dual; }
static void hand_mock(void *ps,int anim,unsigned hand) { ++weapon_calls;observed_ps=ps;observed_anim=anim;observed_hand=hand; }
static void both_mock(void *ps,int anim) { hand_mock(ps,anim,9); }
static int body_mock(void *ps,int anim,int part,int duration,int timer,int cont,int force) {
    assert(ps==cg_array+T7O_MAC_CG_PLAYER_STATE && anim==7 && part==2 && !duration && !timer && cont==1 && force==1);
    ++body_calls;return 100;
}
static void *data_mock(void) { return definitions; }
static int event_mock(void *client,int event,int cont,int force,void *data) {
    assert(client==anim_clients+3*T7O_MAC_SERVER_ANIM_CLIENT_STRIDE && event==99 && !cont && force==1 && data==definitions);
    ++event_calls;return 100;
}
const void *prepatch_original_code(const void *address,size_t size) { (void)address;(void)size;abort(); }

static ScriptValue args[2][4];
static void arguments(unsigned instance,uint32_t id,int a,int b,int c) {
    for (unsigned i=0;i<4;++i) args[instance][3-i]=(ScriptValue){0,7,0};
    args[instance][3].value=id;args[instance][2].value=(uint32_t)a;
    args[instance][1].value=(uint32_t)b;args[instance][0].value=(uint32_t)c;
    write_pointer(pubs+instance*T7O_MAC_VM_INSTANCE_STRIDE+0x20,&args[instance][3]);
    write32(pubs+instance*T7O_MAC_VM_INSTANCE_STRIDE+0x38,4);
}
int main(void) {
    assert(!t7o_mac_script_activate());
    cg_array=calloc(2,T7O_MAC_CG_STRIDE);assert(cg_array);
    original_profile=original;engine_get_int=get_integer;engine_get_string=get_text;
    engine_push_int=result_integer;engine_push_vector=result_vector;engine_error=error_mock;
    engine_view_origin=view_mock;engine_dual_wield=dual_mock;engine_weapon_hand=hand_mock;
    engine_weapon_both=both_mock;engine_body_anim=body_mock;engine_anim_event=event_mock;engine_anim_data=data_mock;
    installed=true;builtin=(Builtin){t7o_script_hash("isprofilebuild"),0,1,0,original,0,0};
    assert(!t7o_mac_script_activate() && builtin.function==original && builtin.max_args==1);
    builtin.max_args=0;assert(t7o_mac_script_activate());
    assert(t7o_mac_script_activate() && builtin.function==dispatch && builtin.max_args==255);
    dispatch(0);assert(original_calls==1);
    for (unsigned instance=0;instance<2;++instance) {
        arguments(instance,0x22ea46c7,0,0,0);
        args[instance][2]=(ScriptValue){(uintptr_t)"STARTWEAPONANIM",2,0};
        dispatch(instance);assert((uint32_t)pushed_int==0xe2a60154);
        arguments(instance,0x35224a7a,1,0,0);
        write32(cg_array+T7O_MAC_CG_STRIDE+T7O_MAC_CG_TIME,5678);
        dispatch(instance);assert(pushed_vector[0]==1 && pushed_vector[2]==3);
        uint64_t weapon=123;memcpy(cg_array+T7O_MAC_CG_PLAYER_STATE+0x2c0,&weapon,8);
        arguments(instance,0xe2a60154,0,15,1);dual=true;
        dispatch(instance);assert(observed_ps==cg_array+T7O_MAC_CG_PLAYER_STATE && observed_anim==15 && observed_hand==1);
        dual=false;dispatch(instance);assert(observed_hand==9);
        arguments(instance,0xe2555348,0,7,2);write32(definitions+0xa0000,8);
        dispatch(instance);assert(body_calls==(int)instance+1);
        write32(definitions+0xa0000,7);int before=errors;
        dispatch(instance);assert(errors==before+1 && body_calls==(int)instance+1);
        arguments(instance,0x50ee1cfb,2,99,0);
        uint8_t client[4];write32(client,3);write_pointer(entities+2*T7O_MAC_SERVER_ENTITY_STRIDE+0x250,client);
        uint16_t type=1;memcpy(entities+2*T7O_MAC_SERVER_ENTITY_STRIDE+0x1a0,&type,2);
        dispatch(instance);assert(event_calls==(int)instance+1);
        write_pointer(entities+2*T7O_MAC_SERVER_ENTITY_STRIDE+0x250,NULL);
        dispatch(instance);assert(event_calls==(int)instance+1); // Guest/no server side effect.
        arguments(instance,0x12345678,0,0,0);before=errors;
        dispatch(instance);assert(errors==before); // Native unknown-ID behavior.
        arguments(instance,0xe2a60154,-1,15,0);dispatch(instance);assert(errors==before+1);
        arguments(instance,0xe2a60154,0,15,2);dispatch(instance);assert(errors==before+2);
        arguments(instance,0x22ea46c7,0,0,0);dispatch(instance);assert(errors==before+3);
    }
    assert(weapon_calls==4 && body_calls==2 && event_calls==2);
    assert(t7o_mac_script_deactivate() && builtin.function==original && builtin.max_args==0);
    assert(!t7o_mac_script_active() && t7o_mac_script_activate());
    builtin.function=original;builtin.max_args=1;
    assert(!t7o_mac_script_deactivate() && builtin.max_args==1); /* preserve a foreign edit */
    free(cg_array);
    puts("Mac CSC adapter argument, layout, animation and fallback contracts passed (substituted engine)");
    return 0;
}
