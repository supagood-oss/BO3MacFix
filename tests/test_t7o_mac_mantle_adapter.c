#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define T7O_DEV_NATIVE 1
static void *test_address(uint32_t);
static uint64_t now=1000;
static uint64_t test_time(void) { return now; }
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_MANTLE_MILLISECONDS test_time
#include "../source/t7o_mac_mantle.c"
static uint32_t flags=0x40,cg_count=2;
static uint8_t cg_data[2*T7O_MAC_CG_STRIDE],movement[0x300],player[0x1000],local[0x100];
static void *cg=cg_data,*enabled=(void *)1;
static const char *setting="57";
static bool gate_result,trace_result,physical_mantle,saw_probe,hud_hint;
static unsigned moves,traces,huds;
static void *test_address(uint32_t offset) {
    switch (offset) {
    case T7O_MAC_SESSION_FLAGS:return &flags;
    case T7O_MAC_MANTLE_ENABLE:return &enabled;
    case T7O_MAC_CG_COUNT:return &cg_count;
    case T7O_MAC_CG_ARRAY:return &cg;
    default:abort();
    }
}
static void put64(void *p,uint64_t v) { memcpy(p,&v,8); }
static bool bool_mock(const void *dvar) { assert(dvar==(void *)1);return true; }
static void *find_mock(const char *name) { assert(!strcmp(name,"aae_t7_mantle_height"));return &setting; }
static const char *text_mock(const void *dvar) { assert(dvar==&setting);return setting; }
static bool gate_mock(void *m,void *l) { assert(m==movement && l==local);return gate_result; }
static bool trace_mock(void *m,void *l,float *a,float *b) {
    assert(m==movement && l==local && a && b);++traces;saw_probe=scope.probe;
    float z=10;T7oInlineContext context={0};memcpy(context.xmm[4],&z,4);
    t7o_mac_mantle_inline_dispatch(&context,T7O_INLINE_MANTLE_HEIGHT);memcpy(&z,context.xmm[4],4);
    assert(z==(scope.probe ? 67:10));return trace_result;
}
static void move_mock(void *m,void *l) {
    ++moves;physical_mantle=false;
    if (gate(m,l)) { float a[3]={0},b[3]={0};if (trace(m,l,a,b)) physical_mantle=true; }
}
static void hud_mock(int client,const void *rect,void *font,void *style) {
    assert(client==0 && rect==local && !font && !style);++huds;hud_hint=(u32(cg_data+0x11b0b4)&0x10)!=0;
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *f,void **o) { (void)p;(void)f;(void)o;abort(); }
bool prepatch_native_site_present(uint32_t site) { (void)site;return true; }
bool prepatch_set_inline_callback(uint32_t site,void (*f)(void)) { assert(site==T7O_MAC_MANTLE_HEIGHT);(void)f;return true; }
void t7o_inline_mantle_height(void) {}
int main(void) {
    assert(!t7o_mac_mantle_install(0,false) && !t7o_mac_mantle_activate());
    base=0x100000000;installed=true;find_dvar=find_mock;dvar_text=text_mock;get_bool=bool_mock;
    original_move=move_mock;original_gate=gate_mock;original_trace=trace_mock;original_hud=hud_mock;
    void *p=player;memcpy(movement,&p,8);assert(t7o_mac_mantle_activate());
    trace_result=true;move(movement,local);assert(!traces && !physical_mantle && state.started[0]==1000);
    now=2999;move(movement,local);assert(!traces);
    now=3000;move(movement,local);assert(traces==1 && saw_probe && !physical_mantle && state.latched[0][0]);
    hud(0,local,NULL,NULL);assert(hud_hint && !u32(cg_data+0x11b0b4));
    trace_result=false;now=3020;move(movement,local);hud(0,local,NULL,NULL);assert(hud_hint && state.latched[0][0]);
    now=3021;move(movement,local);hud(0,local,NULL,NULL);assert(!hud_hint && !state.latched[0][0]);
    gate_result=true;trace_result=true;move(movement,local);assert(physical_mantle && !saw_probe);
    state.latched[0][0]=true;state.seen[0][0]=now;put64(player+0x10,0x200);move(movement,local);
    assert(state.mantling[0] && !state.latched[0][0]);put64(player+0x10,0);
    assert(!t7o_mac_mantle_ui_mode(0,base+T7O_MAC_MANTLE_MODE_RETURN,true));
    assert(t7o_mac_mantle_ui_mode(0,base+T7O_MAC_MANTLE_MODE_RETURN+1,true));
    assert(t7o_mac_mantle_ui_mode(2,base+T7O_MAC_MANTLE_MODE_RETURN,true));
    state.latched[0][1]=true;state.seen[0][1]=now;setting="100";hud(0,local,NULL,NULL);assert(!state.latched[0][1]);setting="57";
    flags=0;unsigned previous=traces;gate_result=false;move(movement,local);assert(traces==previous);flags=0x40;
    movement[0x2ac]=2;move(movement,local);assert(traces==previous);movement[0x2ac]=0;
    t7o_mac_mantle_deactivate();assert(!scope.movement && !state.started[0]);
    assert(t7o_mac_mantle_ui_mode(0,base+T7O_MAC_MANTLE_MODE_RETURN,true));
    printf("Mac mantle hint probe/physical separation, 2 s warmup, 20 ms latch, height/flags/reset and scoped HUD passed (%u movement calls, %u HUD calls)\n",moves,huds);
}
