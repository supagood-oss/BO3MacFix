// macOS replacement for T7Overcharged (https://github.com/JariKCoding/T7Overcharged),
// a Windows DLL that workshop mods such as All-around Enhancement load through Lua.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "havok.h"
#include "offsets.h"
#include "t7overcharged.h"
#include "t7overcharged_lua.h"
#include "t7o_core.h"
#include "t7o_platform.h"
#include "t7o_mac_fx.h"
#include "t7o_mac_script.h"
#include "t7o_mac_movement.h"
#include "t7o_mac_pause.h"
#include "t7o_mac_lifecycle.h"
#include "t7o_mac_presentation.h"
#include "t7o_mac_traversal.h"
#include "t7o_mac_lean.h"
#include "t7o_mac_migration.h"
#include "t7o_mac_ui.h"
#include "t7o_mac_checkpoint.h"
#include "t7o_mac_arc.h"
#include "utilities.h"
#include "mod_fileio.h"
#include "steam.h"

// defined in bo3macfix.c
extern uint64_t game_base_address;
void lua_correct_path(const char *in_path, char *out_path, size_t out_path_len);
// defined in bo3macnative.m
char *copy_clipboard_text();
void set_clipboard_text(const char *text);

#define ASSET_TYPE_COUNT 0x6B
#define MAX_ASSET_LIMIT 100000

typedef T7oAssetPool XAssetPool;
_Static_assert(sizeof(XAssetPool) == 0x20, "XAssetPool must match the game's layout");

// the folder the mod's T7Overcharged DLL is in
static char dll_dir[4096];
static char dll_file[4096];

bool t7o_is_overcharged_dll(const char *path) {
    const char *name = filename_from_path(path);
    return strcasecmp(name, "T7Overcharged.ff") == 0 || strcasecmp(name, "T7Overcharged.dll") == 0;
}

// Raises an asset pool's limit by chaining a new block of entries onto its free list.
static void extend_asset_pool(int type, const char *name, int new_limit) {
    if (type < 0 || type >= ASSET_TYPE_COUNT || new_limit > MAX_ASSET_LIMIT)
        return;
    // make sure the mod means the same asset type as us
    const char *pool_name = ((const char **)(game_base_address + ADDR_g_assetNames))[type];
    if (strcmp(pool_name, name) != 0) {
        printf("T7Overcharged: asset type %i is '%s', not '%s'\n", type, pool_name, name);
        return;
    }
    XAssetPool *pool = (XAssetPool *)(game_base_address + ADDR_DB_XAssetPool) + type;
    int old_limit = pool->item_count;
    void *allocation = NULL;
    if (!t7o_pool_extend(pool, new_limit, &allocation)) {
        printf("T7Overcharged: could not raise %s pool from %i to %i\n", name, old_limit, new_limit);
    } else if (allocation != NULL) {
        // Engine references can outlive the UI VM. Keep this allocation alive.
        printf("T7Overcharged: raised %s limit from %i to %i\n", name, old_limit, new_limit);
    }
}

// Applies the mod's T7Overcharged/assetlimits.txt, made of "<type> <name> <default limit> <new limit>" lines.
static void apply_asset_limits() {
    char path[4200];
    snprintf(path, sizeof(path), "%s/T7Overcharged/assetlimits.txt", dll_dir);
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return;
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        int type, default_limit, new_limit;
        char name[64];
        if (sscanf(line, "%i %63s %i %i", &type, name, &default_limit, &new_limit) == 4)
            extend_asset_pool(type, name, new_limit);
    }
    fclose(fp);
}

static int lua_create_directory(lua_State *s) {
    char path[4096];
    lua_correct_path(hks_check_string(s, 1), path, sizeof(path));
    bool ok = bo3_mkdirs(path);
    hks_push_boolean(s, ok);
    return 1;
}

static int lua_directory_exists(lua_State *s) {
    char path[4096];
    lua_correct_path(hks_check_string(s, 1), path, sizeof(path));
    struct stat st;
    hks_push_boolean(s, stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    return 1;
}

static int lua_clipboard_get(lua_State *s) {
    char *text = copy_clipboard_text();
    hks_push_string(s, text != NULL ? text : "");
    free(text);
    return 1;
}

static int lua_clipboard_set(lua_State *s) {
    set_clipboard_text(hks_check_string(s, 1));
    return 0;
}

static int lua_print(lua_State *s) {
    printf("%s\n", hks_check_string(s, 1));
    return 0;
}

static int lua_native_call(lua_State *s) {
    const char *operation = hks_check_string(s, 1);
    size_t count = (size_t)(HKS_TOP(s) - HKS_BASE(s));
    const char *arguments[7];
    if (count == 0 || count > 8) {
        hks_push_nil(s);
        hks_push_string(s, "invalid BO3MacFix helper arguments");
        return 2;
    }
    for (size_t i = 1; i < count; ++i) arguments[i - 1] = hks_check_string(s, (int)i + 1);
    char json_path[4096];
    if ((!strcmp(operation,"json.read") || !strcmp(operation,"json.write")) && count>1) {
        lua_correct_path(arguments[0],json_path,sizeof(json_path)); arguments[0]=json_path;
    }
    if (strcmp(operation,"file.delete")==0 && count==2) {
        char path[4096];
        lua_correct_path(arguments[0],path,sizeof(path));
        hks_push_string(s,unlink(path)==0 ? "1" : "0"); return 1;
    }
    if (strcmp(operation,"mod.unload")==0 && count==1) {
        int status=t7o_mac_lifecycle_unload();
        if (status<0) {
            hks_push_nil(s);hks_push_string(s,"Mac mod lifecycle adapter is unavailable");return 2;
        }
        hks_push_string(s,status==1 ? "1" : "0");return 1;
    }
    char *result = t7o_platform_call(operation, count - 1, arguments);
    if (result == NULL) {
        hks_push_nil(s);
        hks_push_string(s, "required native operation is unavailable");
        return 2;
    }
    hks_push_string(s, result);
    free(result);
    return 1;
}

// Our version of T7Overcharged's init(): raises the mod's asset limits, then sets up the Lua libraries it provides.
static int t7o_init(lua_State *s) {
    // the stack can be reallocated while our Lua runs, so remember the top as an index
    ptrdiff_t top = HKS_TOP(s) - HKS_BASE(s);
    bool initialized = false;
    t7o_platform_begin_mod(dll_file);
    t7o_platform_ticket_provider(steam_http_ticket_copy);
    char *save_codec=t7o_platform_call("capability",1,(const char *const[]){"save_codec"});
    steam_http_auth_begin(save_codec && !strcmp(save_codec,"1"));
    free(save_codec);
    uint32_t capabilities=0;
    t7o_mac_traversal_deactivate();
    t7o_mac_migration_deactivate();
    t7o_mac_ui_deactivate();
    t7o_mac_checkpoint_deactivate();
    t7o_mac_arc_deactivate();
    char plugin[4200];
    int plugin_length=snprintf(plugin,sizeof(plugin),"%s/T7Overcharged/plugins/AAEUnified.dll",dll_dir);
    if (plugin_length>0 && (size_t)plugin_length<sizeof(plugin) && access(plugin,F_OK)==0) {
        if (!t7o_mac_traversal_activate()) printf("T7Overcharged: AAE traversal hooks are unavailable\n");
        if (!t7o_mac_migration_activate()) printf("T7Overcharged: AAE migration hooks are unavailable\n");
        if (!t7o_mac_ui_activate(s)) printf("T7Overcharged: AAE viewmodel widget hooks are unavailable\n");
        if (!t7o_mac_checkpoint_activate()) printf("T7Overcharged: AAE checkpoint FX repair is unavailable\n");
        if (!t7o_mac_arc_activate()) printf("T7Overcharged: AAE arc display anchors are unavailable\n");
    }
    if (t7o_mac_fx_activate()) capabilities|=T7O_CAP_FX;
    if (t7o_mac_script_activate()) capabilities|=T7O_CAP_CSC;
    if (t7o_mac_movement_activate()) capabilities|=T7O_CAP_MOVEMENT;
    if (t7o_mac_pause_activate()) capabilities|=T7O_CAP_PAUSE;
    if (t7o_mac_lifecycle_ready()) capabilities|=T7O_CAP_UNLOAD;
    if (t7o_mac_presentation_activate()) capabilities|=T7O_CAP_AIM;
    if (!t7o_mac_lean_activate()) printf("T7Overcharged: optional viewmodel lean is unavailable\n");
    t7o_platform_engine_capabilities(capabilities);
    apply_asset_limits();
    if (hks_load_source(s, (const char *)t7overcharged_lua, t7overcharged_lua_len, "=BO3MacFix T7Overcharged")) {
        hks_push_cfunction(s, lua_create_directory, "CreateDirectory");
        hks_push_cfunction(s, lua_directory_exists, "DirectoryExists");
        hks_push_cfunction(s, lua_clipboard_get, "ClipboardGet");
        hks_push_cfunction(s, lua_clipboard_set, "ClipboardSet");
        hks_push_cfunction(s, lua_print, "Print");
        hks_push_cfunction(s, lua_native_call, "NativeCall");
        if (hks_pcall(s, 6, 2)) {
            HksObject *result = HKS_BASE(s) + top;
            initialized = (result->t & 0xf) == HKS_TBOOLEAN && result->v.boolean != 0;
        }
    }
    HKS_TOP(s) = HKS_BASE(s) + top;
    hks_push_boolean(s, initialized);
    if (!initialized) printf("T7Overcharged: initialization failed; mod compatibility is unavailable\n");
    return 1;
}

void t7o_push_init(lua_State *s, const char *dll_path) {
    snprintf(dll_file, sizeof(dll_file), "%s", dll_path);
    strncpy(dll_dir, dll_path, sizeof(dll_dir) - 1);
    char *last_slash = strrchr(dll_dir, '/');
    if (last_slash != NULL)
        *last_slash = '\0';
    hks_push_cfunction(s, t7o_init, "init");
}
