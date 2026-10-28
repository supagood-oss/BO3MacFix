#include "../source/t7o_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t data[32]; unsigned writes; unsigned fail_write; } Memory;
static bool read_memory(void *context, uintptr_t address, void *bytes, size_t n) {
    Memory *m = context;
    if (address > sizeof(m->data) || n > sizeof(m->data) - address) return false;
    memcpy(bytes, m->data + address, n); return true;
}
static bool write_memory(void *context, uintptr_t address, const void *bytes, size_t n) {
    Memory *m = context;
    if (++m->writes == m->fail_write || address > sizeof(m->data) || n > sizeof(m->data) - address)
        return false;
    memcpy(m->data + address, bytes, n); return true;
}
static void test_patch_safety(void) {
    Memory m = {0};
    T7oBytePatch p[] = {{.address=0,.length=2,.original={1,2},.replacement={9,8}},
                       {.address=4,.length=1,.original={3},.replacement={7}}};
    m.data[0]=1; m.data[1]=2; m.data[4]=4; /* Wrong build at the second site. */
    assert(t7o_patch_transaction(p,2,true,read_memory,write_memory,&m)==T7O_PATCH_MISMATCH);
    assert(m.writes==0 && m.data[0]==1);
    m.data[4]=3; m.fail_write=2;
    assert(t7o_patch_transaction(p,2,true,read_memory,write_memory,&m)==T7O_PATCH_WRITE_FAILED);
    assert(m.data[0]==1 && m.data[1]==2 && m.data[4]==3 && !p[0].enabled);
    m.fail_write=0;
    assert(t7o_patch_transaction(p,2,true,read_memory,write_memory,&m)==T7O_PATCH_OK);
    assert(p[0].enabled && p[1].enabled && m.data[0]==9 && m.data[4]==7);
    unsigned writes=m.writes;
    assert(t7o_patch_transaction(p,2,true,read_memory,write_memory,&m)==T7O_PATCH_OK);
    assert(m.writes==writes); /* Repeated setting must not write code again. */
    assert(t7o_patch_transaction(p,2,false,read_memory,write_memory,&m)==T7O_PATCH_OK);
    assert(m.data[0]==1 && m.data[4]==3 && !p[0].enabled);
    p[1].address=1;
    assert(t7o_patch_transaction(p,2,true,read_memory,write_memory,&m)==T7O_PATCH_INVALID);
}
static void test_rip_relocation(void) {
    uint8_t bytes[]={0x55,0x48,0x89,0xe5,0x48,0x8d,0x0d,0x4a,0xc1,0x04,0x10};
    uint8_t original[sizeof(bytes)];memcpy(original,bytes,sizeof(bytes));
    uint64_t next=UINT64_C(0x10136b736),relocated=UINT64_C(0x10260000b);
    assert(t7o_relocate_rip32(bytes,sizeof(bytes),7,next,relocated));
    int32_t disp;memcpy(&disp,bytes+7,4);
    assert(relocated+disp==UINT64_C(0x1113b7880) && !memcmp(bytes,original,7));
    memcpy(original,bytes,sizeof(bytes));
    assert(!t7o_relocate_rip32(bytes,sizeof(bytes),8,next,relocated));
    assert(!t7o_relocate_rip32(bytes,sizeof(bytes),7,next,UINT64_C(0xf000000000)));
    assert(!memcmp(bytes,original,sizeof(bytes)));
    int32_t d=INT32_MIN;memcpy(bytes+7,&d,4);
    assert(t7o_relocate_rip32(bytes,sizeof(bytes),7,UINT64_C(0x80000000),UINT64_C(0x80000000)));
    assert(!t7o_relocate_rip32(bytes,sizeof(bytes),7,0,0));
    d=1;memcpy(bytes+7,&d,4);
    assert(!t7o_relocate_rip32(bytes,sizeof(bytes),7,UINT64_MAX,0));
}
static void test_pool_chain(void) {
    void *sentinel=(void *)(uintptr_t)0x1234, *block=NULL;
    T7oAssetPool pool={.item_size=16,.item_count=2,.item_alloc_count=1,.free_head=sentinel};
    assert(t7o_pool_extend(&pool,5,&block));
    assert(pool.item_count==5 && pool.item_alloc_count==1 && pool.free_head==block);
    void *next=pool.free_head;
    for (int i=0;i<3;i++) { assert(next==(uint8_t *)block+i*16); memcpy(&next,next,sizeof(next)); }
    assert(next==sentinel);
    void *unused=(void *)1;
    assert(t7o_pool_extend(&pool,5,&unused) && unused==NULL);
    assert(!t7o_pool_extend(&pool,100001,&unused));
    free(block);
}
static void test_fx_storage_isolation(void) {
    uint8_t first[1024*16]={0}, second[1024*16]={0};
    first[1023*16+12]=3;second[1023*16+12]=7;
    const void *old[2]={first,second};T7oFxStorage storage={0};
    assert(t7o_fx_prepare(&storage,old,1024,2048));
    assert(((uint8_t *)t7o_fx_slot(&storage,0,1023))[12]==3);
    assert(((uint8_t *)t7o_fx_slot(&storage,1,1023))[12]==7);
    assert(((uint8_t *)t7o_fx_slot(&storage,1,2047))[12]==0);
    assert(!t7o_fx_slot(&storage,2,0) && !t7o_fx_slot(&storage,1,2048));
    assert(t7o_fx_clear(&storage,0));
    assert(((uint8_t *)t7o_fx_slot(&storage,1,1023))[12]==7);
    storage.bound=true;assert(!t7o_fx_discard(&storage));
    storage.bound=false;assert(t7o_fx_discard(&storage));
}
typedef struct { int32_t args[4], result; unsigned calls; float vector[3]; } Script;
static bool get_int(void *c,int inst,unsigned arg,int32_t *v) {
    assert(inst==1); if(arg>=4)return false; *v=((Script *)c)->args[arg]; return true;
}
static const char *get_string(void *c,int inst,unsigned arg) { (void)c;assert(inst==1 && arg==1);return "startweaponanim"; }
static bool push_int(void *c,int inst,int32_t value) { assert(inst==1);((Script *)c)->result=value;return true; }
static bool view_origin(void *c,int inst,int client,float v[3]) { assert(inst==1 && client==0);((Script *)c)->calls++;v[0]=1;v[1]=2;v[2]=3;return true; }
static bool push_vector(void *c,int inst,const float v[3]) { assert(inst==1);memcpy(((Script *)c)->vector,v,12);return true; }
static bool animation(void *c,int inst,int client,int anim,int hand) { assert(inst==1 && client==0 && anim==12 && hand==1);((Script *)c)->calls++;return true; }
static void test_dispatch(void) {
    Script s={0}; T7oScriptBackend b={.context=&s,.get_int=get_int,.get_string=get_string,
      .push_int=push_int,.push_vector=push_vector,.get_view_origin=view_origin,.start_weapon_anim=animation};
    s.args[0]=0x22ea46c7;
    assert(t7o_script_dispatch(&b,1)==T7O_SCRIPT_OK && (uint32_t)s.result==UINT32_C(0xe2a60154));
    s.args[0]=0x35224a7a;
    assert(t7o_script_dispatch(&b,1)==T7O_SCRIPT_OK && s.vector[2]==3);
    s.args[0]=(int32_t)UINT32_C(0xe2a60154);s.args[2]=12;s.args[3]=1;
    assert(t7o_script_dispatch(&b,1)==T7O_SCRIPT_OK && s.calls==2);
    s.args[0]=123;
    assert(t7o_script_dispatch(&b,1)==T7O_SCRIPT_UNSUPPORTED && s.calls==2);
    assert(t7o_script_dispatch(&b,2)==T7O_SCRIPT_BAD_ARGUMENT);
}
static void test_traversal_boundaries(void) {
    /* Golden outputs from the original native arithmetic, including the
     * engine's unchanged low branches. No proprietary image is needed here. */
    const struct { unsigned flags;float height;int result; } rows[]={
        {0,77.999f,9},{0,78,20},{0,331.999f,20},{0,332,21},
        {1,78,17},{1,181.999f,17},{1,182,18},{1,332,19},
        {3,0,15},{7,0,16},{17,0,14},{9,0,12},{1,0,13},
        {2,0.999f,3},{2,1,7},{2,50,10},{6,0.999f,4},{6,1,8},{6,50,11},
        {16,0,2},{16,1,6},{0,0,1},{0,1,5},{0,50,9}
    };
    for (unsigned i=0;i<sizeof(rows)/sizeof(*rows);++i)
        assert(t7o_traversal_type(rows[i].flags,rows[i].height,-18)==rows[i].result);
    assert(t7o_traversal_type(0,51,-100)==20);
    assert(t7o_traversal_type(0,607.999f,600)==9);
    assert(t7o_traversal_type(0,608,600)==20);
    assert(t7o_traversal_type(0,78,NAN)==20);
}
int main(void) {
    /* Golden IDs recovered from the two DLLs, independently of this code. */
    assert(t7o_script_hash("getvieworigin")==UINT32_C(0x35224a7a));
    assert(t7o_script_hash("ANIM_EVENT")==UINT32_C(0x50ee1cfb));
    assert(t7o_script_hash("playanim")==UINT32_C(0xe2555348));
    int8_t f; uint32_t buttons;
    t7o_omni_sprint(-128,-40,UINT32_C(1)<<18,&f,&buttons);
    assert(f==127 && (buttons&(UINT32_C(1)<<30))!=0);
    t7o_omni_sprint(0,0,UINT32_C(1)<<18,&f,&buttons);
    assert(f==0 && (buttons&(UINT32_C(1)<<30))==0);
    assert(t7o_strafe_scale(NAN)==0.667f && t7o_strafe_scale(0.9f)==0.9f);
    bool enabled[T7O_MOVEMENT_COUNT];t7o_movement_defaults(true,enabled);
    for(int i=0;i<T7O_MOVEMENT_COUNT;i++)assert(enabled[i]==(i!=T7O_FAST_SWAP));
    test_rip_relocation();test_patch_safety();test_pool_chain();test_fx_storage_isolation();test_dispatch();test_traversal_boundaries();
    puts("T7 native core tests passed (portable behavior; no Mac bindings qualified)");return 0;
}
