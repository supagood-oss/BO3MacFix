#include "t7o_mac_arc.h"
#include "t7o_mac_offsets.h"
#include "t7o_mac_ui.h"
#include "prepatch.h"
#include "dobby.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static void (*original_draw)(int),(*original_enqueue)(const void *);
static const uint8_t *(*original_definition)(uint64_t);
static void (*original_sample)(float,float,int,const void *);
static uint64_t (*weapon_by_name)(const char *,void *,void *);
static void *(*dvar_find)(const char *);
static const char *(*dvar_string)(const void *);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static int32_t signed32(const void *p) { int32_t v;memcpy(&v,p,4);return v; }
static bool suitable(const uint8_t *current,const uint8_t *candidate) {
    if (!current || !candidate || read32(current+0x6c)!=read32(candidate+0x6c)) return false;
    uint32_t speed=read32(candidate+0xfec),fuse=read32(candidate+0xc28);
    int32_t up=signed32(candidate+0xff0),relative=signed32(candidate+0xff4),forward=signed32(candidate+0xff8);
    float lifetime;memcpy(&lifetime,candidate+0x1004,4);
    if (speed>100000 || up < -100000 || up>100000 || relative < -100000 || relative>100000 ||
        forward < -100000 || forward>100000 || (!speed && !up && !relative && !forward) || fuse>600000 ||
        !isfinite(lifetime) || lifetime<0 || lifetime>100000 || candidate[0x10de]>1 || candidate[0x10c9]>1) return false;
    return !candidate[0x10de] || fuse!=0;
}
static const uint8_t *definition_for(uint64_t weapon,uintptr_t caller) {
    const uint8_t *current=original_definition(weapon);
    if (!atomic_load(&active) || caller!=base+T7O_MAC_ARC_DEFINITION_RETURN || !current) return current;
    void *dvar=dvar_find("aae_arc_weapondef");const char *name=dvar ? dvar_string(dvar) : NULL;
    if (!name || !*name) return current;
    unsigned n=0;
    for (;n<128 && name[n];++n) {
        unsigned c=(unsigned char)name[n];
        if (!(c>='a' && c<='z') && !(c>='A' && c<='Z') && !(c>='0' && c<='9') &&
            c!='_' && c!='-' && c!='.' && c!='/') return current;
    }
    if (n>=128) return current;
    uint64_t selected=weapon_by_name(name,address(T7O_MAC_WEAPON_NAME_TABLE),address(T7O_MAC_WEAPON_NAME_SORTED));
    if (selected<1 || selected>511 || selected>read32(address(T7O_MAC_WEAPON_COUNT))) return current;
    const uint8_t *entry,*candidate;
    memcpy(&entry,(const uint8_t *)address(T7O_MAC_WEAPON_TABLE)+selected*8,sizeof(entry));if (!entry) return current;
    memcpy(&candidate,entry+0x18,sizeof(candidate));
    return suitable(current,candidate) ? candidate : current;
}
static const uint8_t *definition(uint64_t weapon) { return definition_for(weapon,(uintptr_t)__builtin_return_address(0)); }
static void sample_for(float a,float b,int client,const void *curve,uintptr_t caller) {
    if (atomic_load(&active) && (caller==base+T7O_MAC_ARC_SAMPLE_FIRST_RETURN || caller==base+T7O_MAC_ARC_SAMPLE_SECOND_RETURN)) {
        if (client<0 || client>=2 || read32((const uint8_t *)address(T7O_MAC_ARC_COUNTS)+(unsigned)client*0xeec)>=5) return;
    }
    original_sample(a,b,client,curve);
}
static void sample(float a,float b,int client,const void *curve) { sample_for(a,b,client,curve,(uintptr_t)__builtin_return_address(0)); }
typedef struct { int client;unsigned seen[2]; } Draw;
static _Thread_local Draw drawing={.client=-1};
/* The engine retains the descriptor's point pointer after copying its 128-byte
 * header. Match the native persistent 1024-entry ring, not a stack allocation. */
static float points[1024][61][3];
static atomic_uint next_points;
static bool vector_valid(const float p[3]) {
    for (unsigned i=0;i<3;++i) if (!isfinite(p[i]) || fabsf(p[i])>=10000000) return false;
    return true;
}
static void smooth(float path[61][3],const float anchor[3],unsigned count) {
    if (count<=7) return;
    unsigned join=(count+3)/4;if (join<4) join=4;if (join>16) join=16;
    if (join+1>=count) return;
    float direction[3],tangent[3],control[3],side[3],first[3],second[3];
    for (unsigned i=0;i<3;++i) direction[i]=path[join][i]-anchor[i];
    float length=sqrtf(direction[1]*direction[1]+direction[0]*direction[0]+direction[2]*direction[2]);
    if (!isfinite(length) || !(length>0.001f) || !isfinite(length*length)) return;
    for (unsigned i=0;i<3;++i) tangent[i]=path[join+1][i]-path[join][i];
    if (!vector_valid(tangent)) return;
    float amount=(float)join/3;
    for (unsigned i=0;i<3;++i) control[i]=path[join][i]-tangent[i]*amount;
    if (!vector_valid(control)) return;
    for (unsigned i=0;i<3;++i) side[i]=control[i]-anchor[i];
    float projection=(side[1]*direction[1]+side[0]*direction[0]+side[2]*direction[2])/(length*length);
    if (!isfinite(projection)) return;
    for (unsigned i=0;i<3;++i) side[i]-=direction[i]*projection;
    float side_length=sqrtf(side[1]*side[1]+side[0]*side[0]+side[2]*side[2]);
    if (!isfinite(side_length)) return;
    float fraction=projection>0.56f ? projection : 0.56f;if (fraction>=0.9f) fraction=0.9f;
    float scale=1;if (length*0.2f<side_length) scale=(length*0.2f)/side_length;
    for (unsigned i=0;i<3;++i) {
        first[i]=direction[i]*0.27f+anchor[i];
        second[i]=fraction*direction[i]+anchor[i]+side[i]*scale;
    }
    if (!vector_valid(second)) return;
    float replacement[16][3];
    for (unsigned at=1;at<join;++at) {
        float t=(float)at/(float)join,s=1-t;
        float a=s*s*s,b=s*3*s*t,c=s*3*t*t,d=t*t*t;
        for (unsigned i=0;i<3;++i) replacement[at][i]=a*anchor[i]+b*first[i]+c*second[i]+d*path[join][i];
        if (!vector_valid(replacement[at])) return;
    }
    memcpy(path[1],replacement[1],(join-1)*sizeof(replacement[0]));
}
static bool anchored(const uint8_t *input,const float anchor[3],uint8_t output[128]) {
    if (!input || !vector_valid(anchor)) return false;
    unsigned count;const float (*old)[3];float end[3];
    memcpy(&count,input+0x40,4);memcpy(&old,input+0x68,sizeof(old));memcpy(end,input+0x10,12);
    if (count<1 || count>60 || !old || !vector_valid(end)) return false;
    for (unsigned i=0;i<count;++i) if (!vector_valid(old[i])) return false;
    unsigned slot=atomic_fetch_add(&next_points,1)&1023;
    float (*copy)[3]=points[slot];memcpy(copy,old,count*sizeof(copy[0]));memcpy(copy[count],end,12);
    smooth(copy,anchor,count);memcpy(copy[0],anchor,12);memcpy(copy[count],end,12);
    memcpy(output,input,128);memcpy(output+4,anchor,12);memcpy(output+0x68,&copy,sizeof(copy));return true;
}
static void draw(int client) {
    Draw previous=drawing;
    drawing=(Draw){.client=atomic_load(&active) && client>=0 && client<2 ? client : -1};
    original_draw(client);drawing=previous;
}
static void enqueue_for(const void *descriptor,uintptr_t caller) {
    unsigned category=caller==base+T7O_MAC_ARC_FIRST_RETURN ? 0 : 1;
    bool matched=caller==base+T7O_MAC_ARC_FIRST_RETURN || caller==base+T7O_MAC_ARC_SECOND_RETURN;
    if (atomic_load(&active) && drawing.client>=0 && matched) {
        if (!descriptor) return;
        if (drawing.seen[category]++!=0) { original_enqueue(descriptor);return; }
        float anchor[3];uint8_t replacement[128];
        if (t7o_mac_ui_arc_position((unsigned)drawing.client,anchor) && anchored(descriptor,anchor,replacement)) {
            original_enqueue(replacement);return;
        }
    }
    original_enqueue(descriptor);
}
static void enqueue(const void *descriptor) { enqueue_for(descriptor,(uintptr_t)__builtin_return_address(0)); }
void t7o_mac_arc_deactivate(void) { atomic_store(&active,false); }
bool t7o_mac_arc_activate(void) {
    if (!installed) return false;
    atomic_store(&active,true);return true;
}
bool t7o_mac_arc_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t draw_prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57},enqueue_prefix[]={0x55,0x48,0x89,0xe5,0x53};
    const uint8_t definition_prefix[]={0x55,0x48,0x89,0xe5,0x81,0xe7,0xff,0x01,0,0};
    void *draw_address=(void *)(uintptr_t)(base+T7O_MAC_ARC_DRAW),*enqueue_address=(void *)(uintptr_t)(base+T7O_MAC_ARC_ENQUEUE);
    if (memcmp(prepatch_original_code(draw_address,sizeof(draw_prefix)),draw_prefix,sizeof(draw_prefix)) ||
        memcmp(prepatch_original_code(enqueue_address,sizeof(enqueue_prefix)),enqueue_prefix,sizeof(enqueue_prefix)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_WEAPON_DEFINITION),sizeof(definition_prefix)),definition_prefix,sizeof(definition_prefix)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_ARC_SAMPLE),sizeof(draw_prefix)),draw_prefix,sizeof(draw_prefix))) return false;
    weapon_by_name=address(T7O_MAC_WEAPON_BY_NAME);dvar_find=address(T7O_MAC_DVAR_FIND_NAME);dvar_string=address(T7O_MAC_DVAR_VALUE_STRING);
    if (DobbyHook(draw_address,(void *)draw,(void **)&original_draw)!=0 ||
        DobbyHook(enqueue_address,(void *)enqueue,(void **)&original_enqueue)!=0 ||
        DobbyHook(address(T7O_MAC_WEAPON_DEFINITION),(void *)definition,(void **)&original_definition)!=0 ||
        DobbyHook(address(T7O_MAC_ARC_SAMPLE),(void *)sample,(void **)&original_sample)!=0) return false;
    installed=true;return true;
}
