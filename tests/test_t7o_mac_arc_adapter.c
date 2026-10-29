#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define T7O_DEV_NATIVE 1
static void *test_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#include "../source/t7o_mac_arc.c"
static uint8_t descriptor[128];static float path[60][3],anchor[3]={2,3,4};
static unsigned enqueued,anchored_count,anchor_requests;
static bool ready=true;
static uint8_t current_def[0x1100],candidate_def[0x1100],entry[32],counts[0x1dd8],name_table[4],sorted[1];
static const uint8_t *weapon_table[512];static uint32_t weapon_count=5,selected_weapon=2;
static const char *override_name="example_weapon";static unsigned sampled;
static void *test_address(uint32_t rva) {
    switch (rva) {
    case T7O_MAC_WEAPON_NAME_TABLE:return name_table;
    case T7O_MAC_WEAPON_NAME_SORTED:return sorted;
    case T7O_MAC_WEAPON_TABLE:return weapon_table;
    case T7O_MAC_WEAPON_COUNT:return &weapon_count;
    case T7O_MAC_ARC_COUNTS:return counts;
    default:abort();
    }
}
static const uint8_t *definition_mock(uint64_t weapon) { assert(weapon==42);return current_def; }
static uint64_t weapon_mock(const char *name,void *table,void *flag) {
    assert(!strcmp(name,override_name) && table==name_table && flag==sorted);return selected_weapon;
}
static void *find_mock(const char *name) { assert(!strcmp(name,"aae_arc_weapondef"));return &override_name; }
static const char *string_mock(const void *p) { return *(const char *const *)p; }
static void sample_mock(float a,float b,int client,const void *curve) { assert(a==2 && b==3 && client==1 && curve==entry);++sampled; }
static void put32(void *p,uint32_t value) { memcpy(p,&value,4); }
static void candidate(void) { memset(candidate_def,0,sizeof(candidate_def));put32(candidate_def+0xfec,10); }
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
    original_definition=definition_mock;weapon_by_name=weapon_mock;dvar_find=find_mock;dvar_string=string_mock;original_sample=sample_mock;
    const uint8_t *def=candidate_def;memcpy(entry+0x18,&def,sizeof(def));weapon_table[2]=entry;candidate();
    assert(definition_for(42,base+T7O_MAC_ARC_DEFINITION_RETURN)==current_def);
    assert(t7o_mac_arc_activate() && definition_for(42,base+T7O_MAC_ARC_DEFINITION_RETURN)==candidate_def);
    assert(definition_for(42,base+T7O_MAC_ARC_DEFINITION_RETURN+1)==current_def);
    override_name="invalid name";assert(definition_for(42,base+T7O_MAC_ARC_DEFINITION_RETURN)==current_def);override_name="example_weapon";
    selected_weapon=6;assert(definition_for(42,base+T7O_MAC_ARC_DEFINITION_RETURN)==current_def);selected_weapon=2;
    for (unsigned bad=0;bad<12;++bad) {
        candidate();float value=0;
        switch (bad) {
        case 0:put32(candidate_def+0x6c,1);break;
        case 1:put32(candidate_def+0xfec,100001);break;
        case 2:put32(candidate_def+0xff0,(uint32_t)-100001);break;
        case 3:put32(candidate_def+0xff4,100001);break;
        case 4:put32(candidate_def+0xff8,(uint32_t)-100001);break;
        case 5:put32(candidate_def+0xc28,600001);break;
        case 6:value=NAN;memcpy(candidate_def+0x1004,&value,4);break;
        case 7:value=-1;memcpy(candidate_def+0x1004,&value,4);break;
        case 8:value=100001;memcpy(candidate_def+0x1004,&value,4);break;
        case 9:put32(candidate_def+0xfec,0);break;
        case 10:candidate_def[0x10de]=1;break;
        case 11:candidate_def[0x10c9]=2;break;
        }
        assert(definition_for(42,base+T7O_MAC_ARC_DEFINITION_RETURN)==current_def);
    }
    put32(counts+0xeec,4);sample_for(2,3,1,entry,base+T7O_MAC_ARC_SAMPLE_FIRST_RETURN);assert(sampled==1);
    put32(counts+0xeec,5);sample_for(2,3,1,entry,base+T7O_MAC_ARC_SAMPLE_SECOND_RETURN);assert(sampled==1);
    sample_for(2,3,1,entry,base+T7O_MAC_ARC_SAMPLE_SECOND_RETURN+1);assert(sampled==2);
    t7o_mac_arc_deactivate();sample_for(2,3,1,entry,base+T7O_MAC_ARC_SAMPLE_FIRST_RETURN);assert(sampled==3);
    puts("AAE arc: smoothing/lifetime, per-pass and unload fallback, scoped weapon definition validation, five-segment guard and Mac argument forwarding passed (substituted engine)");
}
