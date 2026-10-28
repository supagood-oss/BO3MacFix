#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static void *test_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_lean.c"
static uint8_t ui[2*0x1078],cg[2][T7O_MAC_CG_TIME+4];
static int keys[2][256],key_calls,bone_calls,tag_calls,alloc_calls;
static bool is_main=true,has_bone=true;
static const char *enabled="1",*left="81",*right="69",*angles="0 0 25";
static void *test_address(uint32_t rva) { assert(rva==T7O_MAC_CLIENT_UI);return ui; }
static void *find_mock(const char *name) {
    return !strcmp(name,"leaning_enabled") ? &enabled : !strcmp(name,"leaning_key_left") ? &left :
        !strcmp(name,"leaning_key_right") ? &right : !strcmp(name,"lean_ang") ? &angles : NULL;
}
static const char *text_mock(const void *p) { return *(const char *const *)p; }
static void set_mock(const char *a,const char *b,bool c) { (void)a;(void)b;(void)c;abort(); }
static bool main_mock(void) { return is_main; }
static int key_mock(int client,int key) { assert(client>=0 && client<2 && key>0 && key<256);return keys[client][key]; }
static void key_original(int client,int key,int down,int time) { (void)time;++key_calls;keys[client][key]=down; }
static void bones_original(void *a,void *b,void *c,void *d,void *e) { (void)a;(void)b;(void)c;(void)d;(void)e;++bone_calls; }
static unsigned string_mock(const char *s) { assert(!strcmp(s,"tag_ads"));++alloc_calls;return 123; }
static int index_mock(void *object,unsigned tag,uint16_t *bone,unsigned mask) {
    assert(object==(void *)1 && tag==123 && *bone==0xfffe && mask==UINT32_MAX);*bone=7;return has_bone;
}
static void tag_mock(void *object,const uint32_t *bits,uint16_t bone,const float *origin,const float *value) {
    assert(object==(void *)1 && bone==7 && !origin[0] && !origin[1] && !origin[2]);
    for (unsigned i=0;i<12;++i) assert(bits[i]==UINT32_MAX);
    assert(isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]));++tag_calls;
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
static void put32(void *p,int v) { memcpy(p,&v,4); }
static void render(int client,int time) {
    put32(cg[client]+4,client);put32(cg[client]+T7O_MAC_CG_TIME,time);
    update_bones(cg[client],NULL,(void *)1,NULL,NULL);
}
int main(void) {
    find_dvar=find_mock;dvar_text=text_mock;set_string=set_mock;main_thread=main_mock;
    key_down=key_mock;original_key=key_original;original_bones=bones_original;
    alloc_string=string_mock;bone_index=index_mock;set_tag=tag_mock;
    assert(!t7o_mac_lean_install(1,false) && !t7o_mac_lean_activate());
    installed=true;assert(t7o_mac_lean_activate());put32(ui+8,11);put32(ui+0x1078+8,11);
    key_event(0,81,1,0);assert(!clients[0].moving && keys[0][81]);
    key_event(0,81,0,0);assert(clients[0].moving && clients[0].target[2]==-25 && !keys[0][81]);
    render(0,100);assert(fabsf(clients[0].current[2]+0.28125f)<0.00001f && tag_calls==1 && alloc_calls==1);
    for (int i=0;i<13;++i) render(0,101+i);
    assert(clients[0].current[2]==-25 && !clients[0].moving && alloc_calls==1);
    assert(!clients[1].current[2]);
    key_event(0,81,1,0);key_event(0,81,0,0);
    assert(clients[0].target[2]==0 && clients[0].curve==2 && clients[0].duration==0.6f);
    for (int i=0;i<9;++i) render(0,200+i);
    assert(!clients[0].current[2] && !clients[0].moving);
    ui[4]=1;key_event(0,69,1,0);key_event(0,69,0,0);assert(!clients[0].moving);ui[4]=0;
    key_event(1,69,1,0);key_event(1,69,0,0);assert(clients[1].target[2]==25 && !clients[0].moving);
    render(1,100);assert(alloc_calls==2);
    key_event(1,160,1,0);key_event(1,160,0,0);assert(!clients[1].target[2]);
    enabled="0";int before=tag_calls;render(1,101);assert(tag_calls==before);enabled="1";
    is_main=false;render(1,102);assert(tag_calls==before);is_main=true;
    has_bone=false;render(1,103);assert(tag_calls==before);has_bone=true;
    render(1,1);assert(alloc_calls==3 && !clients[1].current[2]);
    float out[3];angles="nan 0 1";assert(!angles_setting(out));angles="0 0 999";
    assert(angles_setting(out) && out[2]==180);left="9999999999999999999999999";
    key_event(0,81,1,0);key_event(0,81,0,0);assert(!clients[0].moving);
    t7o_mac_lean_deactivate();before=tag_calls;render(0,300);assert(tag_calls==before && !clients[0].moving);
    assert(key_calls>0 && bone_calls>0);
    puts("Expanded lean: key-up ordering, toggle/easing, fire reset, UI/main-thread guards, split-client isolation, missing bones, map/unload reset passed (substituted engine)");
}
