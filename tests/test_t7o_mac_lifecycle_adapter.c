#include <assert.h>
#include <stdio.h>
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_lifecycle.c"
static const char *disabled="0";
static int stage, requests, resets;
static const void *expected_record;
static void *find_mock(const char *name) { assert(!strcmp(name,"disable_unloadmod"));return &disabled; }
static const char *string_mock(const void *p) { return *(const char *const *)p; }
static void load_mock(int controller,const void *record,bool reload) {
    assert(controller==0 && record==expected_record && reload && stage==0);stage=1;
}
static void by_name_mock(int controller,const char *name,bool reload) {
    assert(controller==0 && name && !*name && reload);++requests;
}
void t7o_mac_movement_deactivate(void) { assert(stage==1);stage=2; }
void t7o_mac_pause_deactivate(void) { assert(stage==2);stage=3; }
bool t7o_mac_script_deactivate(void) { assert(stage==3);stage=4;return true; }
void t7o_mac_presentation_deactivate(void) { assert(stage==4);stage=5; }
void t7o_mac_traversal_deactivate(void) { assert(stage==5);stage=6; }
void t7o_mac_lean_deactivate(void) { assert(stage==6);stage=7; }
void t7o_mac_migration_deactivate(void) { assert(stage==7);stage=8; }
void t7o_platform_begin_mod(const char *path) { assert(stage==8 && !path);stage=9;++resets; }
void steam_http_auth_begin(bool enabled) { assert(stage==9 && !enabled);stage=10; }
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
int main(void) {
    char name[16]="workshop_mod";base=(uintptr_t)name-T7O_MAC_LOADED_MOD_NAME;
    dvar_find=find_mock;dvar_string=string_mock;load_by_name=by_name_mock;original_load=load_mock;
    assert(t7o_mac_lifecycle_unload()==-1);installed=true;
    disabled="1";assert(t7o_mac_lifecycle_unload()==0 && !requests);
    disabled="0";name[0]=0;assert(t7o_mac_lifecycle_unload()==2 && !requests);
    name[0]='m';assert(t7o_mac_lifecycle_unload()==1 && requests==1);
    int record=42;expected_record=&record;load_core(0,&record,true);
    assert(stage==10 && resets==1);
    assert(!t7o_mac_lifecycle_install(base,false));
    puts("Mac unload arguments, disable/no-mod guards and ordered lifecycle reset passed (substituted engine)");
}
