#include "t7o_mac_lifecycle.h"
#include "t7o_mac_offsets.h"
#include "t7o_mac_movement.h"
#include "t7o_mac_pause.h"
#include "t7o_mac_script.h"
#include "t7o_mac_presentation.h"
#include "t7o_mac_traversal.h"
#include "t7o_mac_lean.h"
#include "t7o_mac_migration.h"
#include "t7o_mac_ui.h"
#include "t7o_mac_checkpoint.h"
#include "t7o_platform.h"
#include "steam_auth.h"
#include "prepatch.h"
#include "dobby.h"
#include <stdlib.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static void (*original_load)(int,const void *,bool);
static void (*load_by_name)(int,const char *,bool);
static void *(*dvar_find)(const char *);
static const char *(*dvar_string)(const void *);
static void *address(uint32_t offset) { return (void *)(uintptr_t)(base+offset); }
static void load_core(int controller,const void *record,bool reload) {
    original_load(controller,record,reload);
    /* The core schedules mod/UI loading; the replacement DLL's init occurs
     * later. Leave the extended FX cache alive: its engine reset hook clears
     * records when the engine reaches the cache's own lifetime boundary. */
    t7o_mac_movement_deactivate();t7o_mac_pause_deactivate();
    t7o_mac_script_deactivate();t7o_mac_presentation_deactivate();t7o_mac_traversal_deactivate();
    t7o_mac_lean_deactivate();t7o_mac_migration_deactivate();t7o_mac_ui_deactivate();
    t7o_mac_checkpoint_deactivate();t7o_platform_begin_mod(NULL);
    steam_http_auth_begin(false);
}
int t7o_mac_lifecycle_unload(void) {
    if (!installed) return -1;
    void *dvar=dvar_find("disable_unloadmod");
    const char *value=dvar ? dvar_string(dvar) : NULL;
    if (value && strtol(value,NULL,10)!=0) return 0;
    if (!*(const char *)address(T7O_MAC_LOADED_MOD_NAME)) return 2;
    /* The by-name wrapper takes a string. The hooked core takes a mod record;
     * passing an empty string directly to the core would corrupt its reads. */
    load_by_name(0,"",true);return 1;
}
bool t7o_mac_lifecycle_ready(void) { return installed; }
bool t7o_mac_lifecycle_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t core[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    const uint8_t by_name[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    if (memcmp(prepatch_original_code(address(T7O_MAC_LOAD_MOD_CORE),sizeof(core)),core,sizeof(core)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_LOAD_MOD_NAME),sizeof(by_name)),by_name,sizeof(by_name))) return false;
    load_by_name=(void *)address(T7O_MAC_LOAD_MOD_NAME);
    dvar_find=(void *)address(T7O_MAC_DVAR_FIND_NAME);dvar_string=(void *)address(T7O_MAC_DVAR_VALUE_STRING);
    if (DobbyHook(address(T7O_MAC_LOAD_MOD_CORE),(void *)load_core,(void **)&original_load)!=0) return false;
    installed=true;return true;
}
