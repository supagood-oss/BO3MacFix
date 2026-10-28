#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "../source/t7o_mac_offsets.h"
static void *fixture_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) fixture_address(offset)
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_presentation.c"
static uint8_t *clients;static int count=2;
static uint8_t definition[0xe68];
static int observed=-1,publications,original_calls,zoom,level;
static const char *glyphs="0";
static const char *force_crosshair="0";
static int crosshair_calls;
static bool crosshair_result;
static void *find_mock(const char *name) {
    if (!strcmp(name,"t7o_force_3rd_person_crosshair")) return &force_crosshair;
    assert(!strcmp(name,"t7o_override_gamepadtype"));return &glyphs;
}
static bool crosshair_mock(const void *client,const void *player) {
    assert(client==&count && player==definition);++crosshair_calls;return crosshair_result;
}
static const char *text_mock(const void *value) { return *(const char *const *)value; }
static void *fixture_address(uint32_t offset) {
    if (offset==T7O_MAC_CG_ARRAY) return &clients;
    assert(offset==T7O_MAC_CG_COUNT);return &count;
}
static void hint_mock(int client,const void *rectangle,void *font,void *style) {
    (void)client;assert(rectangle==&count && font==definition && style==&count);++original_calls;
}
static uint64_t weapon_mock(void *player) { assert(player==clients+T7O_MAC_CG_PLAYER_STATE);return 42; }
static const uint8_t *definition_mock(uint64_t weapon) { assert(weapon==42);return definition; }
static int zoom_mock(uint64_t weapon) { assert(weapon==42);return zoom; }
static int level_mock(uint64_t weapon) { assert(weapon==42);return level; }
static void set_mock(const char *name,const char *value,bool force) {
    assert(!strcmp(name,"aim_status") && force);observed=atoi(value);++publications;
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
static void put32(void *p,int32_t value) { memcpy(p,&value,4); }
static void draw(void) { hold_breath_hint(0,&count,definition,&count); }
int main(void) {
    assert(!t7o_mac_presentation_activate());
    find_dvar=find_mock;dvar_text=text_mock;
    original_crosshair=crosshair_mock;
    assert(!crosshair_visible(&count,definition) && crosshair_calls==1);
    assert(t7o_mac_presentation_gamepad(2)==2);
    original_hint=hint_mock;view_weapon=weapon_mock;weapon_definition=definition_mock;
    zoom_count=zoom_mock;zoom_level=level_mock;set_string=set_mock;
    clients=calloc(2,T7O_MAC_CG_STRIDE);assert(clients);
    put32(clients+T7O_MAC_BREATH_SPENT,-1);
    installed=true;assert(t7o_mac_presentation_activate() && observed==0);draw();
    assert(!crosshair_visible(&count,definition) && crosshair_calls==2);
    force_crosshair="1";assert(crosshair_visible(&count,definition) && crosshair_calls==2);
    force_crosshair="0";crosshair_result=true;
    assert(crosshair_visible(&count,definition) && crosshair_calls==3);
    crosshair_result=false;
    glyphs="1";assert(t7o_mac_presentation_gamepad(2)==1);
    glyphs="2";assert(t7o_mac_presentation_gamepad(1)==0);
    glyphs="3";assert(t7o_mac_presentation_gamepad(0)==2);
    glyphs="0";assert(t7o_mac_presentation_gamepad(1)==1);
    glyphs="2bad";assert(t7o_mac_presentation_gamepad(1)==1);
    glyphs="2";
    assert(original_calls==1 && publications==1);
    uint8_t *ps=clients+T7O_MAC_CG_PLAYER_STATE;
    float fraction=1.0f;memcpy(ps+0x2fc,&fraction,4);
    put32(clients+T7O_MAC_BREATH_SPENT,-1);draw();assert(observed==1);
    zoom=level=1;draw();assert(observed==4);
    definition[0xdc8]=1;draw();assert(observed==2);
    ps[0x18]=4;put32(clients+T7O_MAC_BREATH_SPENT,200);put32(clients+T7O_MAC_BREATH_LIMIT,1000);
    draw();assert(observed==3);
    put32(clients+T7O_MAC_BREATH_SPENT,1001);draw();assert(observed==9);
    put32(clients+T7O_MAC_BREATH_SPENT,-1);put32(clients+T7O_MAC_BREATH_RECOVERY,20);
    put32(clients+T7O_MAC_CG_FRAME_MSEC,20);draw();assert(observed==6);
    ps[0x18]=0;put32(clients+T7O_MAC_BREATH_SPENT,0);draw();assert(observed==7);
    put32(clients+T7O_MAC_BREATH_SPENT,4500);draw();assert(observed==8);
    assert(t7o_aim_status_tail(2,false,4480,0,100,20)==2);
    assert(t7o_aim_status_tail(2,false,4481,0,100,20)==8);
    assert(t7o_aim_status_tail(1,false,INT32_MAX,0,100,1)==1);
    t7o_mac_presentation_deactivate();assert(observed==0);int before=publications;draw();
    force_crosshair="1";
    assert(!crosshair_visible(&count,definition) && crosshair_calls==4);
    assert(t7o_mac_presentation_gamepad(1)==1);
    assert(publications==before && original_calls==10);
    free(clients);puts("Mac aim/breath state, recovery boundaries, original hints and mod reset passed (substituted engine)");
}
