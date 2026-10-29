#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_arc.c"
static uint8_t descriptor[128];static float path[60][3],anchor[3]={2,3,4};
static unsigned enqueued,anchored_count,anchor_requests;
static bool ready=true;
bool t7o_mac_ui_arc_position(unsigned client,float out[3]) {
    assert(client<2);++anchor_requests;memcpy(out,anchor,12);return ready;
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
static void put_count(unsigned count) { memcpy(descriptor+0x40,&count,4); }
static void enqueue_mock(const void *p) {
    const uint8_t *bytes=p;const float (*positions)[3];memcpy(&positions,bytes+0x68,sizeof(positions));
    ++enqueued;
    if (memcmp(bytes+4,anchor,12)==0) {
        assert(positions!=path && !memcmp(positions[0],anchor,12));
        assert(!memcmp(positions[59],descriptor+0x10,12));++anchored_count;
    } else assert(p==descriptor);
}
static void draw_mock(int client) {
    assert(drawing.client==client || !atomic_load(&active));
    if (atomic_load(&active)) enqueue_for(NULL,base+T7O_MAC_ARC_FIRST_RETURN);
    for (unsigned category=0;category<2;++category) for (unsigned duplicate=0;duplicate<2;++duplicate)
        enqueue_for(descriptor,base+(category ? T7O_MAC_ARC_SECOND_RETURN : T7O_MAC_ARC_FIRST_RETURN));
    enqueue_for(descriptor,base+T7O_MAC_ARC_SECOND_RETURN+1);
}
int main(void) {
    const float (*p)[3]=path;memcpy(descriptor+0x68,&p,sizeof(p));put_count(59);
    for (unsigned i=0;i<60;++i) { path[i][0]=i*3;path[i][1]=(float)(i*i)/20;path[i][2]=i*2; }
    memcpy(descriptor+0x10,path[59],12);
    assert(!t7o_mac_arc_install(0,false) && !t7o_mac_arc_activate());
    installed=true;assert(t7o_mac_arc_activate());base=0x100000000;original_enqueue=enqueue_mock;original_draw=draw_mock;
    draw(0);assert(enqueued==5 && anchored_count==2 && anchor_requests==2 && drawing.client==-1);
    float (*first)[3]=points[0];assert(!memcmp(first[0],anchor,12));
    assert(!memcmp(first[15],path[15],12) && memcmp(first[1],path[1],12));
    ready=false;draw(1);assert(enqueued==10 && anchored_count==2 && anchor_requests==4);ready=true;
    uint8_t output[128];put_count(0);assert(!anchored(descriptor,anchor,output));
    put_count(61);assert(!anchored(descriptor,anchor,output));put_count(59);
    path[8][0]=NAN;assert(!anchored(descriptor,anchor,output));path[8][0]=24;
    float saved[61][3];memcpy(saved,first,sizeof(saved));
    t7o_mac_arc_deactivate();draw(0);assert(enqueued==15 && anchored_count==2 && anchor_requests==4);
    assert(!memcmp(saved,first,sizeof(saved))); /* Retained points survive the call and deactivation. */
    puts("AAE arc: smoothed start with original join/end, persistent point lifetime, per-pass caller/duplicate guards, missing snapshots and unload fallback passed (substituted engine)");
}
