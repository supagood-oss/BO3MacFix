#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
static void *test_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_traversal.c"
static uint8_t entities[1024*T7O_MAC_SERVER_ENTITY_STRIDE];
static uint8_t clients[2*T7O_MAC_SERVER_CLIENT_STRIDE];
static void *client_base=clients;
static const char *height="57",*offset="-18",*input="1",*ignore="0";
static unsigned defaults,originals,vehicle_calls,callbacks;
static int permission;
static uint32_t fail_site;
static void *test_address(uint32_t rva) {
    if (rva==T7O_MAC_SERVER_ENTITIES) return entities;
    if (rva==T7O_MAC_SERVER_CLIENTS) return &client_base;
    return (void *)(uintptr_t)(base+rva);
}
static void *find_mock(const char *name) {
    const char **value=!strcmp(name,"aae_t7_mantle_height") ? &height :
        !strcmp(name,"aae_t7_high_height_offset") ? &offset :
        !strcmp(name,"aae_t7_move_input_threshold") ? &input :
        !strcmp(name,"aae_t7_ignore_mantle_checks") ? &ignore : NULL;
    assert(value);return *value ? value : NULL;
}
static const char *text_mock(const void *p) { return *(const char *const *)p; }
static void set_mock(const char *name,const char *value,bool force) {
    assert(!strcmp(name,"aae_t7_move_input_threshold") && !strcmp(value,"1") && force);++defaults;input=value;
}
static int type_mock(const void *p,Point point) { (void)p;(void)point;++originals;return 42; }
static float setting_mock(const char *name,bool session) { (void)name;(void)session;++originals;return 99; }
static int permission_mock(const void *a,const void *b) {
    assert(a==entities+100*T7O_MAC_SERVER_ENTITY_STRIDE && b==entities+T7O_MAC_SERVER_ENTITY_STRIDE);
    ++vehicle_calls;return permission;
}
void t7o_inline_traversal_input(void) {}
void t7o_inline_traversal_eligible(void) {}
void t7o_inline_traversal_surface(void) {}
bool prepatch_set_inline_callback(uint32_t rva,void (*callback)(void)) {
    assert(rva==T7O_MAC_TRAVERSAL_INPUT || rva==T7O_MAC_TRAVERSAL_ELIGIBLE || rva==T7O_MAC_TRAVERSAL_SURFACE);
    if (callback && rva==fail_site) return false;
    callbacks+=callback!=NULL;return true;
}
bool prepatch_native_site_present(uint32_t rva) { (void)rva;abort(); }
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
static void put32(void *p,uint32_t v) { memcpy(p,&v,4); }
static void put16(void *p,uint16_t v) { memcpy(p,&v,2); }
static void put_pointer(void *p,void *v) { memcpy(p,&v,sizeof(v)); }
int main(void) {
    find_dvar=find_mock;dvar_text=text_mock;set_string=set_mock;original_type=type_mock;
    original_setting=setting_mock;vehicle_permission=permission_mock;base=0x100000000;
    assert(!t7o_mac_traversal_install(base,false) && !t7o_mac_traversal_activate());
    uint8_t player[0x800]={0};Point point={0,0,78};
    assert(traversal_type(player,point)==42);
    installed=true;input=NULL;assert(t7o_mac_traversal_activate() && defaults==1 && callbacks==3);
    assert(t7o_mac_traversal_activate() && defaults==1); /* Preserve existing dvars. */
    assert(traversal_type(player,point)==20);point.z=77.999f;assert(traversal_type(player,point)==9);
    point.z=NAN;assert(traversal_type(player,point)==42);
    assert(game_setting_for("trm_maxHeight",false,base+T7O_MAC_TRAVERSAL_HEIGHT_RETURN)==57);
    height="123";assert(game_setting_for("trm_maxHeight",false,base+T7O_MAC_TRAVERSAL_HEIGHT_RETURN)==123);
    height="4097";assert(game_setting_for("trm_maxHeight",false,base+T7O_MAC_TRAVERSAL_HEIGHT_RETURN)==57);
    assert(game_setting_for("anotherSetting",false,base+T7O_MAC_TRAVERSAL_HEIGHT_RETURN)==99);
    assert(game_setting_for("trm_maxHeight",false,base+1)==99);
    const char *values[]={"-99","-1","0","1","50","50.9","256","999","nan","bad"};
    int limits[]={-1,-1,0,1,50,50,256,256,0,0};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);++i) {
        input=values[i];
        for (uint32_t sum=0;sum<=256;++sum) {
            T7oInlineContext c={0};c.r12=sum;c.rax=sum/2;c.rcx=sum-sum/2;c.flags=0x6d7;
            t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_INPUT);
            assert(c.r12==(uint64_t)(sum && (int)sum>limits[i] ? 51 : 0));
            assert(c.rax==sum/2 && c.rcx==sum-sum/2 && c.flags==0x6d7);
            t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_ELIGIBLE);
            assert((uint32_t)(c.rcx+c.rax)==(uint32_t)(limits[i]<=0 || (int)sum>limits[i] ? 51 : 0));
        }
    }
    uint8_t frame[0x300]={0},movement[0x350]={0};
    uint8_t *trace=frame,*ps=clients+T7O_MAC_SERVER_CLIENT_STRIDE;
    uint8_t *vehicle=entities+100*T7O_MAC_SERVER_ENTITY_STRIDE;
    T7oInlineContext c={0};c.rbp=(uintptr_t)(frame+0x160);c.r13=(uintptr_t)movement;c.r14=(uintptr_t)ps;
    put32(ps,1);put32(ps+0x16bf8,1);put16(vehicle+0x1a0,15);put_pointer(vehicle+0x270,vehicle);
    put_pointer(entities+T7O_MAC_SERVER_ENTITY_STRIDE+0x250,ps);
    put32(trace+0x24,1);put16(trace+0x28,100);
    put32(trace+0x14,0x18000000);put32(trace+0x18,0x1000001);permission=4;
    t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_SURFACE);
    assert(read32(trace+0x18)==0x1000001 && !read32(ps+0x7f4) && vehicle_calls==1);
    permission=1;t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_SURFACE);
    assert(read32(trace+0x18)==1 && read32(ps+0x7f4)==1 && vehicle_calls==2);
    ignore="1";permission=4;put32(trace+0x18,0x1000001);
    t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_SURFACE);
    assert(read32(trace+0x18)==1 && vehicle_calls==2);
    /* The native unknown-context branch reverses the vanilla bit-24 test.
     * Its animation flag uses a different word, at trace +0x14. */
    ignore="0";put32(trace+0x24,0);put32(trace+0x18,0x1000001);put32(ps+0x7f4,0);
    t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_SURFACE);
    assert(read32(trace+0x18)==1 && read32(ps+0x7f4)==1 && vehicle_calls==2);
    put32(trace+0x14,0);put32(ps+0x7f4,0);put32(trace+0x18,0x19000000);
    t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_SURFACE);
    assert(!read32(ps+0x7f4) && read32(trace+0x18)==0x18000000);
    put32(trace+0x18,0);t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_SURFACE);
    assert(read32(trace+0x18)==0x1000000);
    put32(trace+0x24,1);
    ignore="0";movement[0x2ac]=1;
    assert(vault_permission(trace,ps,movement)==0); /* Authoritative vehicle denial. */
    permission=1;assert(vault_permission(trace,ps,movement)==1);
    assert(vault_permission(trace,ps+4,movement)==0); /* Misaligned authoritative PS. */
    put_pointer(entities+T7O_MAC_SERVER_ENTITY_STRIDE+0x250,NULL);
    assert(vault_permission(trace,ps,movement)==0);
    movement[0x2ac]=0;assert(vault_permission(trace,ps,movement)==-1);
    client_base=NULL;assert(vault_permission(trace,ps,movement)==-1);client_base=clients;
    t7o_mac_traversal_deactivate();assert(!atomic_load(&active) && traversal_type(player,point)==42);
    T7oInlineContext saved=c;t7o_mac_traversal_inline_dispatch(&c,T7O_INLINE_TRAVERSAL_INPUT);
    assert(!memcmp(&saved,&c,sizeof(c)));
    fail_site=T7O_MAC_TRAVERSAL_ELIGIBLE;assert(!t7o_mac_traversal_activate() && !atomic_load(&active));
    puts("AAE traversal: 5,140 threshold paths, height/caller gates, vehicle permissions and unload fallback passed (substituted engine)");
}
