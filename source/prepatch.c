// The pre-patched executable, see prepatch.h.
//
// In the copy BO3MacFix_BuildPrepatched writes:
// - BO3MacFix's byte patches are already applied
// - every hooked function starts with a jump to a stub in __TEXT's tail padding:
//       cmp qword [rip+slot], 0 ; je trampoline ; jmp [rip+slot]
//       trampoline: <the instructions the jump replaced> ; jmp <function + their length>
//   so hooking it only means storing a pointer in its slot, in __DATA's zero-filled tail
// - each "mov r32, imm32" a setting changes jumps to a stub that loads the value from a slot when it's set
// - each call through objc_msgSend's pointer a setting redirects calls a stub that jumps through a slot when it's set
// - a header lists the sites; hook originals come from the matching compiled
//   table, as do setting/call originals; byte-patch records retain their bytes
// With every slot left at 0 the copy runs exactly like the original.
#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/loader.h>

#define PREPATCH_IMPL // the real DobbyHook/DobbyCodePatch, not the redirects in dobby.h
#include "dobby.h"
#include "offsets.h"
#include "prepatch.h"
#include "t7o_mac_offsets.h"
#include "t7o_core.h"
#include "t7o_codegen.h"

#define EXPORT __attribute__((visibility("default")))

#define Prepatch_Magic "BO3MFPP1"
#define Prepatch_Version 4
#define Game_Base 0x100000000ULL
/* Verified zero padding between load commands (ending 0x17f8) and the first
 * __TEXT section (0x2100). No load command or game section is overwritten. */
#define Native_Code_Start 0x1800u
#define Native_Code_End 0x2100u
/* These x86 stubs contain no aligned instruction-address contract. Byte
 * packing retains the verified section bounds without adding a segment. */
#define Hook_Code_Alignment 1u

#pragma mark - what goes into the copy

// Whole instructions replaced by the entry jump. The explicitly identified
// client-pause LEA, strafe float load and migration relative call are relocated
// below; other entries have no relative instructions in their replaced prefix.
typedef struct {
    uint32_t function;
    uint8_t length;
    uint8_t short_jcc_at; // 1 + the offset of a short conditional jump among them, made near in the trampoline
    uint8_t stolen[16];
} hook_def_t;

#define Push_R15 6, 0, { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57 } // push rbp; mov rbp, rsp; push r15
#define Push_R14 6, 0, { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56 } // push rbp; mov rbp, rsp; push r14

// every function BO3MacFix hooks
static const hook_def_t hook_defs[] = {
#if T7O_DEV_NATIVE
    { T7O_MAC_MANTLE_MOVE,Push_R15 },
    { T7O_MAC_MANTLE_GATE,Push_R15 },
    { T7O_MAC_MANTLE_TRACE,Push_R15 },
    { T7O_MAC_MANTLE_HUD,Push_R15 },
    { T7O_MAC_SESSION_IS_MODE,10,0,{0x55,0x48,0x89,0xe5,0x8b,0x05,0xaa,0xea,0x6b,0x05} },
    { T7O_MAC_FREEAIM_PROJECTILE,Push_R15 },
    { T7O_MAC_FREEAIM_SPREAD,Push_R15 },
    { T7O_MAC_FREEAIM_GRENADE,Push_R15 },
    { T7O_MAC_FREEAIM_ROCKET,Push_R15 },
    { T7O_MAC_FREEAIM_BULLET,Push_R15 },
    { T7O_MAC_CREATE_USERCMD,Push_R14 },
    { T7O_MAC_EXECUTE_CLIENT_COMMAND,Push_R15 },
    { T7O_MAC_CHECKPOINT_FRAME,Push_R15 },
    { T7O_MAC_CHECKPOINT_SERVER_LOAD,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_MESSAGE_STRING,Push_R14 },
    { T7O_MAC_CAMERA_DRAW,Push_R15 },
    { T7O_MAC_ANGLES_TO_AXIS,8,0,{0x55,0x48,0x89,0xe5,0xf2,0x0f,0xf0,0x27} },
    { T7O_MAC_CHECKPOINT_CLIENT_SAVE,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_CHECKPOINT_CLIENT_LOAD,Push_R15 },
    { T7O_MAC_SERVER_FRAME,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_CLIENT_FRAME,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_CHECK_PAUSED,Push_R14 },
    { T7O_MAC_TOKENIZE,Push_R14 },
    { T7O_MAC_ARC_SAMPLE,Push_R15 },
    { T7O_MAC_WEAPON_DEFINITION,10,0,{0x55,0x48,0x89,0xe5,0x81,0xe7,0xff,0x01,0,0} },
    { T7O_MAC_ARC_DRAW,Push_R15 },
    { T7O_MAC_ARC_ENQUEUE,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_FX_CHECKPOINT_RESTORE,7,0,{0x55,0x48,0x89,0xe5,0x48,0x85,0xf6} },
    { T7O_MAC_BONE_MATRICES,8,0,{0x55,0x48,0x89,0xe5,0x48,0x8b,0x47,0x58} },
    { T7O_MAC_MIGRATION_CHECK,9,0,{0x55,0x48,0x89,0xe5,0xe8,0xd1,0xd5,0xe6,0xff} },
    { T7O_MAC_SELECT_HOST,Push_R15 },
    { T7O_MAC_MIGRATE_CLIENTS,Push_R15 },
    { T7O_MAC_VIEWMODEL_BONES,Push_R15 },
    { T7O_MAC_KEY_EVENT,Push_R15 },
    { T7O_MAC_TRAVERSAL_TYPE,9,0,{0x55,0x48,0x89,0xe5,0xf3,0x0f,0x5c,0x4f,0x38} },
    { T7O_MAC_GAME_SETTING,Push_R15 },
    { T7O_MAC_POOL_REGISTER,Push_R15 },
    { T7O_MAC_CROSSHAIR_VISIBLE,Push_R15 },
    { T7O_MAC_HOLD_BREATH_HINT,Push_R15 },
    { T7O_MAC_JUMP_PENALTY,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_APPLY_JUMP_PENALTY,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_JUMP,Push_R15 },
    { T7O_MAC_LAND_SEVERITY,6,0,{0x55,0x48,0x89,0xe5,0x31,0xc9} },
    { T7O_MAC_SLIDE_ELIGIBLE,5,0,{0x55,0x48,0x89,0xe5,0x53} },
    { T7O_MAC_CLAMP_PITCH,9,0,{0x55,0x48,0x89,0xe5,0xf3,0x0f,0x2a,0x4e,0x10} },
    { T7O_MAC_FORCE_VIEW,6,0,{0x55,0x48,0x89,0xe5,0x85,0xf6} },
    { T7O_MAC_FRICTION, Push_R15 },
    { T7O_MAC_ACCELERATE, Push_R15 },
    { T7O_MAC_FRICTION_SCALE, 12,0,{0x55,0x48,0x89,0xe5,0xf3,0x0f,0x10,0x05,0x75,0xa1,0x7c,0} },
    { T7O_MAC_LOAD_MOD_CORE, Push_R15 },
    { T7O_MAC_SLIDE_ACCELERATE, Push_R15 },
    { T7O_MAC_STRAFE_SCALE, 12, 0, { 0x55,0x48,0x89,0xE5,0xF3,0x0F,0x10,0x05,0x3B,0xB3,0x82,0x00 } },
    { T7O_MAC_UPDATE_SPRINT, Push_R15 },
    { T7O_MAC_CLIENT_CAN_PAUSE, 11, 0, { 0x55,0x48,0x89,0xE5,0x48,0x8D,0x0D,0x4A,0xC1,0x04,0x10 } },
    { T7O_MAC_SERVER_CAN_PAUSE, 5, 0, { 0x55,0x48,0x89,0xE5,0x53 } },
    { T7O_MAC_FX_REGISTER_DEF, Push_R15 },
    { T7O_MAC_FX_GET_POINTER, 6, 0, { 0x55,0x48,0x89,0xE5,0x89,0xF8 } },
    { T7O_MAC_FX_GET_NAME, 6, 0, { 0x55,0x48,0x89,0xE5,0x89,0xF8 } },
    { T7O_MAC_FX_GET_INDEX, 7, 0, { 0x55,0x48,0x89,0xE5,0x48,0x89,0xF0 } },
    { T7O_MAC_FX_UNREGISTER, 7, 0, { 0x55,0x48,0x89,0xE5,0x48,0x89,0xF0 } },
    { T7O_MAC_CACHE_RESET, 5, 0, { 0x55,0x48,0x89,0xE5,0x53 } },
#endif
    { ADDR_ASL_SleepEx, Push_R15 },
    { ADDR_ASL_ToNativePath, Push_R15 },
    { ADDR_ASL_XInputGetState, Push_R14 },
    { ADDR_ASL_XInputSetState, Push_R14 },
    { ADDR_CL_FirstSnapshot, Push_R15 },
    { ADDR_CL_SetCGameTime, Push_R15 },
    { ADDR_Com_GetBuildIntField, 5, 4, { 0x83, 0xFF, 0x02, 0x74, 0x11 } }, // cmp edi, 2; je +0x11
    { ADDR_CSteamAPIContext_Init, Push_R15 },
    { ADDR_DB_GetStreamFraction, Push_R15 },
    { ADDR_dwInstantMessageDispatch, Push_R15 },
    { ADDR_GPad_GetGamepadType, 7, 0, { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x63, 0xC7 } }, // ...; movsxd rax, edi
    { ADDR_HKAI_InitMapData, Push_R15 },
    { ADDR_hks_load_dll, Push_R15 },
    { ADDR_hksf_fopen_internal, 6, 0, { 0x55, 0x48, 0x89, 0xE5, 0x31, 0xC0 } }, // ...; xor eax, eax
    { ADDR_Live_SystemInfo, Push_R15 },
    { ADDR_load_workshop_texture, Push_R15 },
    { ADDR_LobbyMsgRW_PrepReadMsg, Push_R14 },
    { ADDR_LobbyMsgRW_PrepWriteMsg, Push_R14 },
    { ADDR_Lua_GetBuildIntField, 5, 0, { 0x55, 0x48, 0x89, 0xE5, 0x53 } }, // ...; push rbx
    { ADDR_Lua_GetBuildStringField, Push_R14 },
    { ADDR_SDL_SetWindowTitle, Push_R14 },
    { ADDR_someLoggingFunction, Push_R15 },
    { ADDR_Sys_CheckSumPacketCopy, 10, 0, { 0x66, 0xB8, 0xFF, 0xFF, 0x81, 0xFA, 0xE8, 0x04, 0x00, 0x00 } }, // mov ax, 0xFFFF; cmp edx, 0x4E8
    { ADDR_Sys_VerifyPacketChecksum, Push_R15 },
};

// a "mov r32, imm32" that a setting changes, with the flags dead after it (its stub tests the slot)
typedef struct {
    uint32_t site;
    uint8_t original[8];
    uint8_t kind; /* 0 mov; 1 cmp; 2 force Jcc taken; 3 force fall-through */
    uint8_t length; /* zero means the original five-byte MOV form */
} value_def_t;

static const value_def_t value_defs[] = {
#if T7O_DEV_NATIVE
    { T7O_MAC_MANTLE_HEIGHT,{0x41,0x0f,0x28,0xda,0xf3,0x0f,0x59,0xda},5,8 },
    { T7O_MAC_FREEAIM_TRACE_BEGIN,{0x48,0x69,0xce,0xb0,0x08,0,0},5,7 },
    { T7O_MAC_FREEAIM_TRACE_END,{0x48,0x8b,0x9d,0xe0,0xfe,0xff,0xff},5,7 },
    { T7O_MAC_PROMPT_POSITION,{0x41,0x0f,0x10,0x46,0x34},5,5 },
    { T7O_MAC_PROMPT_RESET,{0xf3,0x0f,0x10,0x6d,0xac},5,5 },
    { T7O_MAC_PROMPT_CLEAR_MODEL,{0x83,0xbb,0x54,0x01,0,0,0xfe},5,7 },
    { T7O_MAC_FREEAIM_RETICLE,{0xe8,0xca,0xd0,0x77,0},5,5 },
    { T7O_MAC_FREEAIM_ADS,{0x41,0x0f,0x2e,0x46,0x0c},5,5 },
    { T7O_MAC_FREEAIM_FADE,{0xf3,0x0f,0x59,0x45,0xa0},5,5 },
    { T7O_MAC_FREEAIM_SPREAD_CALL_A,{0xe8,0xe1,0xdc,0xff,0xff},5,5 },
    { T7O_MAC_FREEAIM_SPREAD_CALL_B,{0xe8,0xae,0xd9,0xff,0xff},5,5 },
    { T7O_MAC_VIEWMODEL_OBJECT,{0x49,0x8b,0x3f,0x0f,0x57,0xc0},5,6 },
    { T7O_MAC_TRAVERSAL_INPUT,{0x31,0xc0,0x66,0x41,0xf7,0xc7,0x20,0x08},5,8 },
    { T7O_MAC_TRAVERSAL_ELIGIBLE,{0x01,0xc1,0x83,0xf9,0x33},5,5 },
    { T7O_MAC_TRAVERSAL_SURFACE,{0xf6,0x85,0xbb,0xfe,0xff,0xff,0x01},5,7 },
    { T7O_MAC_SLIDE_FRICTION_LOAD,{0xf3,0x0f,0x10,0x05,0xb2,0x4d,0x6a,0},7,8 },
    { T7O_MAC_SWAP_TIMING_A,{0xf3,0x0f,0x5e,0x05,0x3b,0x77,0x96,0},7,8 },
    { T7O_MAC_SWAP_TIMING_B,{0xf3,0x0f,0x5e,0x05,0xb3,0x75,0x96,0},7,8 },
    { T7O_MAC_SWAP_ANIMATION,{0xf3,0x0f,0x5e,0x05,0x27,0xa8,0xab,0},7,8 },
    { T7O_MAC_SWAP_PRESENTATION,{0xf3,0x0f,0x59,0x05,0xa7,0x94,0xab,0},7,8 },
    { T7O_MAC_JUMP_CAP_BRANCH,{0x76,0x47,0xf3,0x0f,0x51,0xd2},6,6 },
    { T7O_MAC_LAND_BEGIN,{0x4d,0x8b,0x24,0x24,0x4c,0x89,0xe7},5,7 },
    { T7O_MAC_LAND_END,{0x48,0x8d,0x9d,0xa0,0xfe,0xff,0xff},5,7 },
    { T7O_MAC_JUMP_PENALTY_BRANCH,{0x0f,0x84,0x42,0x02,0,0},3,6 },
    { T7O_MAC_SPRINT_LEAP_COMPARE_A, {0x81,0xf9,0xdc,0x05,0,0}, 1,6 },
    { T7O_MAC_ADS_MODE_BRANCH_A, {0x74,0x20,0x45,0x8b,0x7c,0x24,0x68}, 3,7 },
    { T7O_MAC_ADS_MODE_BRANCH_B, {0x74,0x0c,0x0f,0x28,0x45,0xc0}, 3,6 },
    { T7O_MAC_ADS_SCALE, {0xf3,0x0f,0x58,0xc0,0x0f,0x29,0x45,0xc0}, 4,8 },
    { T7O_MAC_SPRINT_LEAP_COMPARE_B, {0x3d,0xdc,0x05,0,0}, 1,5 },
    { T7O_MAC_SPRINT_LERP_COMPARE, {0x3d,0x5d,0x01,0,0}, 1,5 },
    { T7O_MAC_JUMP_INTERVAL_COMPARE, {0x81,0xf9,0xf4,0x01,0,0}, 1,6 },
    { T7O_MAC_SLIDE_SLOW_BRANCH, {0x7d,0x2e,0x0f,0x57,0xc0}, 2,5 },
    { T7O_MAC_SLIDE_SPRINT_BRANCH, {0x0f,0x84,0x4b,0x01,0,0}, 3,6 },
#endif
    { ADDR_JobThreadsDvar_DefaultInstr, { 0xBA, 0x02, 0x00, 0x00, 0x00 } },      // mov edx, 2
    { ADDR_JQ_IdleWait_TimeoutInstr, { 0xBE, 0x01, 0x00, 0x00, 0x00 } },         // mov esi, 1
    { ADDR_ASL_FramesInFlightSemaphoreInstr, { 0xBF, 0x02, 0x00, 0x00, 0x00 } }, // mov edi, 2
};

// a call qword [rip+x] through objc_msgSend's pointer that a setting redirects
typedef struct {
    uint32_t site;
    uint8_t original[6];
} call_def_t;

static const call_def_t call_defs[] = {
    { ADDR_ASL_Present_PresentDrawableCall, { 0xFF, 0x15, 0xD9, 0xDB, 0x87, 0x01 } },
    { ADDR_ASL_Present_CopyToDrawableCall, { 0xFF, 0x15, 0xDC, 0xDD, 0x87, 0x01 } },
};

// a byte patch BO3MacFix always makes (dylib_main and network_version_patch in bo3macfix.c, display_mode_patch)
typedef struct {
    uint32_t offset;
    uint32_t length;
    const char *original;
    const char *patched; // NULL for nops
} patch_def_t;

static const patch_def_t patch_defs[] = {
    { ADDR_Scr_ErrorInternalPatch + 4, 1, "\x01", "\x00" },
    { ADDR_Lua_CmdParseArgs, 1, "\x55", "\xC3" },
    { ADDR_Curl_ContentLengthSetStart, ADDR_Curl_ContentLengthSetEnd - ADDR_Curl_ContentLengthSetStart,
      "\xE8\x97\x3F\x09\x00\x48\x8B\x95\x38\xEF\xFF\xFF\x48\x89\x82\x88\x00\x00\x00", NULL },
    { ADDR_HKAI_InitMapData_DemonWithinCheck, 5, "\xE8\x2B\xDD\x81\x00", "\xB8\x00\x00\x00\x00" },
    { ADDR_Lua_CoD_LuaCall_GetProtocolVersion_Inst + 3, 4, "\x00\xC4\x90\x46", "\x00\xC8\x90\x46" },
    { ADDR_LPC_GetRemoteManifest_Category_Inst + 6, 4, "\x14\x05\x00\x00", "\x28\x05\x00\x00" },
    { ADDR_LobbyHostMsg_SendJoinRequest_GetChangelistCall, 5, "\xE8\xC0\x46\xCE\xFF", "\xB8\x12\xFC\xD3\x00" },
    { ADDR_ASL_BuildModeTable_WidthCap, 2, "\x7F\xDF", "\x66\x90" },
    { ADDR_ASL_BuildModeTable_HeightCap, 2, "\x7F\xDA", "\x66\x90" },
    { ADDR_ASL_DXGIOutput_GetModes_WidthCap, 6, "\x0F\x8F\x18\x01\x00\x00", "\x66\x0F\x1F\x44\x00\x00" },
    { ADDR_ASL_DXGIOutput_GetModes_HeightCap, 6, "\x0F\x8F\x0F\x01\x00\x00", "\x66\x0F\x1F\x44\x00\x00" },
};

// network_version_patch: the network version and the "tu30" string, each found within 16 bytes of these
static const uint32_t netver_addresses[] = { ADDRS_NetworkVersion };
static const uint32_t tuver_addresses[] = { ADDRS_TUVersion };
static const uint16_t netver_original = 18530, netver_patched = 18532;
static const char tuver_original[4] = "tu30", tuver_patched[4] = "tu32";

#pragma mark - the copy's header

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t table_id;     // table_id() of the BO3MacFix that made it
    uint32_t hook_count;
    uint32_t value_count;
    uint32_t call_count;
    uint32_t patch_count;
    uint32_t hooks_offset; // the records, as offsets from the game's base address
    uint32_t values_offset;
    uint32_t calls_offset;
    uint32_t patches_offset;
    uint8_t source_sha256[CC_SHA256_DIGEST_LENGTH]; // the original it was made from
} prepatch_header_t;

typedef struct {
    uint32_t function;
    uint32_t slot;
    uint32_t trampoline;
    uint8_t length;
    uint8_t pad[3];
} prepatch_hook_t;
_Static_assert(sizeof(prepatch_hook_t)==16,"compact hook record");

// a value or call site
typedef struct {
    uint32_t site;
    uint32_t slot;
    uint8_t length;
    uint8_t pad[3];
} prepatch_site_t;
_Static_assert(sizeof(prepatch_site_t)==12,"compact setting/call record");

// byte patch records follow as { uint32_t offset; uint8_t length; uint8_t original[length]; }

static uint32_t fnv1a(uint32_t hash, const void *data, size_t size) {
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ ((const uint8_t *)data)[i]) * 16777619u;
    return hash;
}

// identifies everything above, so the launcher remakes the copy when a new BO3MacFix changes any of it
static uint32_t table_id(void) {
    uint32_t hash = fnv1a(2166136261u, &(uint32_t){ Prepatch_Version }, sizeof(uint32_t));
    hash = fnv1a(hash, &(uint32_t){ Hook_Code_Alignment }, sizeof(uint32_t));
    hash = fnv1a(hash, hook_defs, sizeof(hook_defs));
    hash = fnv1a(hash, value_defs, sizeof(value_defs));
    hash = fnv1a(hash, call_defs, sizeof(call_defs));
    for (size_t i = 0; i < sizeof(patch_defs) / sizeof(patch_defs[0]); i++) {
        hash = fnv1a(hash, &patch_defs[i].offset, sizeof(uint32_t));
        hash = fnv1a(hash, patch_defs[i].original, patch_defs[i].length);
        hash = fnv1a(hash, patch_defs[i].patched ? patch_defs[i].patched : "\x90", patch_defs[i].patched ? patch_defs[i].length : 1);
    }
    hash = fnv1a(hash, netver_addresses, sizeof(netver_addresses));
    hash = fnv1a(hash, tuver_addresses, sizeof(tuver_addresses));
    hash = fnv1a(hash, &netver_patched, sizeof(netver_patched));
    return fnv1a(hash, tuver_patched, sizeof(tuver_patched));
}

#pragma mark - running from the copy

static uint64_t base = 0;
static uint64_t text_end = 0, image_end = 0;
static const prepatch_header_t *header = NULL;
static const prepatch_hook_t *hooks = NULL;
static const prepatch_site_t *values = NULL, *calls = NULL;
static int hooks_set = 0, values_set = 0, calls_set = 0, patches_in_place = 0, in_memory = 0, refused = 0;

bool prepatch_active(void) {
    return header != NULL;
}

// finds where the game's __TEXT segment and its whole image end in memory
static void find_image_bounds(uint64_t base_address) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)base_address;
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    uint64_t end = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (strcmp(seg->segname, "__TEXT") == 0)
                text_end = base_address + seg->vmsize;
            if (strcmp(seg->segname, "__PAGEZERO") != 0 && seg->vmaddr + seg->vmsize > end)
                end = seg->vmaddr + seg->vmsize;
        }
        lc = (const struct load_command *)((const uint8_t *)lc + lc->cmdsize);
    }
    image_end = base_address + (end - Game_Base);
}

bool prepatch_init(uint64_t base_address) {
    base = base_address;
    find_image_bounds(base_address);
    const prepatch_header_t *h = (const prepatch_header_t *)(base + ADDR_Prepatch_Header);
    if (memcmp(h->magic, Prepatch_Magic, sizeof(h->magic)) != 0)
        return false;
    if (h->version != Prepatch_Version) {
        printf("BO3MacFix: the pre-patched executable has format %u, this BO3MacFix needs %u\n", h->version, Prepatch_Version);
        return false;
    }
    /* Format 4 reconstructs originals from this library's verified tables.
     * The launcher already rebuilds its cache whenever that table changes. */
    if (h->table_id!=table_id() || h->hook_count!=sizeof(hook_defs)/sizeof(hook_defs[0]) ||
        h->value_count!=sizeof(value_defs)/sizeof(value_defs[0]) || h->call_count!=sizeof(call_defs)/sizeof(call_defs[0])) {
        printf("BO3MacFix: start the game through Steam to rebuild its pre-patched executable\n");
        return false;
    }
    const prepatch_hook_t *records=(const void *)(base+h->hooks_offset);
    for (uint32_t i=0;i<h->hook_count;++i)
        if (records[i].function!=hook_defs[i].function || records[i].length!=hook_defs[i].length) return false;
    const prepatch_site_t *vr=(const void *)(base+h->values_offset),*cr=(const void *)(base+h->calls_offset);
    for (uint32_t i=0;i<h->value_count;++i)
        if (vr[i].site!=value_defs[i].site || vr[i].length!=(value_defs[i].length ? value_defs[i].length:5) ||
            vr[i].pad[0]!=value_defs[i].kind) return false;
    for (uint32_t i=0;i<h->call_count;++i)
        if (cr[i].site!=call_defs[i].site || cr[i].length!=6) return false;
    header = h;
    hooks = (const prepatch_hook_t *)(base + h->hooks_offset);
    values = (const prepatch_site_t *)(base + h->values_offset);
    calls = (const prepatch_site_t *)(base + h->calls_offset);
    printf("BO3MacFix: running from the pre-patched executable (%u hooks, %u settings, %u calls, %u byte patches)\n",
           h->hook_count, h->value_count, h->call_count, h->patch_count);
    return true;
}

typedef struct {
    uint64_t start, end;
    uint8_t *buffer;
    bool changed;
} overlay_t;

static void overlay(uint64_t at, const uint8_t *original, size_t length, overlay_t *o) {
    for (size_t i = 0; i < length; i++) {
        uint64_t a = at + i;
        if (a >= o->start && a < o->end) {
            o->buffer[a - o->start] = original[i];
            o->changed = true;
        }
    }
}

const void *prepatch_original_code(const void *address, size_t size) {
    static uint8_t buffer[64];
    if (!prepatch_active() || size > sizeof(buffer))
        return address;
    overlay_t o = { (uint64_t)address, (uint64_t)address + size, buffer, false };
    memcpy(buffer, address, size);
    for (uint32_t i = 0; i < header->hook_count; i++)
        overlay(base + hooks[i].function, hook_defs[i].stolen, hook_defs[i].length, &o);
    for (uint32_t i = 0; i < header->value_count; i++)
        overlay(base + values[i].site, value_defs[i].original, values[i].length, &o);
    for (uint32_t i = 0; i < header->call_count; i++)
        overlay(base + calls[i].site, call_defs[i].original, calls[i].length, &o);
    const uint8_t *p = (const uint8_t *)(base + header->patches_offset);
    for (uint32_t i = 0; i < header->patch_count; i++) {
        uint32_t offset;
        memcpy(&offset, p, sizeof(offset));
        overlay(base + offset, p + 5, p[4], &o);
        p += 5 + p[4];
    }
    return o.changed ? buffer : address;
}

static bool in_game_code(const void *address) {
    return (uint64_t)address >= base && (uint64_t)address < text_end;
}

static bool in_game_image(const void *address) {
    return (uint64_t)address >= base && (uint64_t)address < image_end;
}

// replaces DobbyHook (see dobby.h): in the pre-patched copy, hooking a function only stores its slot
int BO3MacFix_Hook(void *address, void *fake_func, void **out_origin_func) {
    if (!prepatch_active() || !in_game_image(address))
        return DobbyHook(address, fake_func, out_origin_func);
    uint32_t offset = (uint32_t)((uint64_t)address - base);
    for (uint32_t i = 0; i < header->hook_count; i++) {
        if (hooks[i].function == offset) {
            *(void **)(base + hooks[i].slot) = fake_func;
            *out_origin_func = (void *)(base + hooks[i].trampoline);
            hooks_set++;
            return 0;
        }
    }
    // a hook the copy doesn't have: hooking it in memory still works, but brings back Rosetta's JIT
    if (prepatch_original_code(address, 16) != address) {
        refused++;
        printf("BO3MacFix: can't hook %08x, the pre-patched executable changed that code\n", offset);
        return -1;
    }
    in_memory++;
    printf("BO3MacFix: the pre-patched executable has no hook at %08x, hooking it in memory\n", offset);
    return DobbyHook(address, fake_func, out_origin_func);
}

// replaces DobbyCodePatch (see dobby.h): in the pre-patched copy, nothing writes the game's code
int BO3MacFix_CodePatch(void *address, uint8_t *buffer, uint32_t buffer_size) {
    if (!prepatch_active() || !in_game_image(address))
        return DobbyCodePatch(address, buffer, buffer_size);
    uint32_t offset = (uint32_t)((uint64_t)address - base);
    if (!in_game_code(address)) {
        // the game's own data, which is writable anyway
        memcpy(address, buffer, buffer_size);
        return 0;
    }
    if (memcmp(address, buffer, buffer_size) == 0) {
        patches_in_place++;
        return 0;
    }
    for (uint32_t i = 0; i < header->value_count; i++) {
        const prepatch_site_t *v = &values[i];
        if (v->pad[0]==0 && v->site == offset && buffer_size == v->length && buffer[0] == value_defs[i].original[0]) {
            uint32_t value;
            memcpy(&value, buffer + 1, sizeof(value));
            *(uint32_t *)(base + v->slot) = value;
            values_set++;
            return 0;
        }
    }
    // a patch the copy doesn't have: making it in memory still works, but brings back Rosetta's JIT
    if (prepatch_original_code(address, buffer_size) != address) {
        refused++;
        printf("BO3MacFix: can't patch %08x, the pre-patched executable changed that code\n", offset);
        return -1;
    }
    in_memory++;
    printf("BO3MacFix: the pre-patched executable doesn't have the patch at %08x, patching it in memory\n", offset);
    return DobbyCodePatch(address, buffer, buffer_size);
}

bool prepatch_redirect_call(uint32_t offset, void *function) {
    if (!prepatch_active())
        return false;
    for (uint32_t i = 0; i < header->call_count; i++) {
        if (calls[i].site == offset) {
            *(void **)(base + calls[i].slot) = function;
            calls_set++;
            return true;
        }
    }
    return false;
}

bool prepatch_native_site_present(uint32_t offset) {
    if (!prepatch_active() || header->table_id!=table_id()) return false;
    for (uint32_t i=0;i<header->value_count;++i)
        if (values[i].site==offset && values[i].pad[0]!=0) return true;
    return false;
}
bool prepatch_set_native_value(uint32_t offset,uint32_t value,bool enabled) {
    if (!prepatch_native_site_present(offset)) return false;
    for (uint32_t i=0;i<header->value_count;++i) if (values[i].site==offset &&
        (values[i].pad[0]<=4 || values[i].pad[0]==7)) {
        uint64_t setting=enabled ? UINT64_C(0x100000000)|value : 0;
        __atomic_store_n((uint64_t *)(uintptr_t)(base+values[i].slot),setting,__ATOMIC_RELEASE);
        return true;
    }
    return false;
}
bool prepatch_set_inline_callback(uint32_t offset,void (*callback)(void)) {
    if (!prepatch_native_site_present(offset)) return false;
    for (uint32_t i=0;i<header->value_count;++i) if (values[i].site==offset &&
        (values[i].pad[0]==5 || values[i].pad[0]==6)) {
        __atomic_store_n((uint64_t *)(uintptr_t)(base+values[i].slot),(uintptr_t)callback,__ATOMIC_RELEASE);return true;
    }
    return false;
}

// how many of the game's code pages have been written to (copied on write) since it was loaded
static int written_code_pages(void) {
    int written = 0;
    mach_vm_address_t address = base;
    while (address < text_end) {
        mach_vm_size_t size = 0;
        vm_region_extended_info_data_t info;
        mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
        mach_port_t object = MACH_PORT_NULL;
        if (mach_vm_region(mach_task_self(), &address, &size, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&info,
                           &count, &object) != KERN_SUCCESS || address >= text_end)
            break;
        written += info.pages_dirtied;
        address += size;
    }
    return written;
}

void prepatch_report(void) {
    if (!prepatch_active())
        return;
    printf("BO3MacFix: pre-patched executable: %i of %u hooks set, %i settings, %i calls redirected, %i patches "
           "already in place, %i done in memory, %i refused, game code pages written: %i\n",
           hooks_set, header->hook_count, values_set, calls_set, patches_in_place, in_memory, refused,
           written_code_pages());
}

#pragma mark - building the copy

typedef struct {
    uint8_t *data;              // the copy
    size_t size;
    uint32_t claimed[512][2];   // ranges already changed, to catch overlaps
    int claimed_count;
    uint8_t records[4096];      // the byte patch records
    size_t records_size;
    uint32_t patch_count;
    char *message;
    size_t message_size;
    uint32_t tail_code_end;
} build_t;

static int fail(char *message, size_t message_size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(message, message_size, format, args);
    va_end(args);
    return -1;
}

static int claim(build_t *b, uint32_t offset, uint32_t length, const char *what) {
    for (int i = 0; i < b->claimed_count; i++) {
        if (offset < b->claimed[i][1] && b->claimed[i][0] < offset + length)
            return fail(b->message, b->message_size, "%s at %08x overlaps another change", what, offset);
    }
    if (b->claimed_count == sizeof(b->claimed) / sizeof(b->claimed[0]) || offset + length > b->size)
        return fail(b->message, b->message_size, "%s at %08x doesn't fit", what, offset);
    b->claimed[b->claimed_count][0] = offset;
    b->claimed[b->claimed_count][1] = offset + length;
    b->claimed_count++;
    return 0;
}

// applies a byte patch and records the original bytes
static int patch(build_t *b, uint32_t offset, const void *original, const void *patched, uint32_t length) {
    if (offset + length > b->size || memcmp(b->data + offset, original, length) != 0)
        return fail(b->message, b->message_size, "unexpected code at %08x", offset);
    if (claim(b, offset, length, "a byte patch") != 0)
        return -1;
    if (b->records_size + 5 + length > sizeof(b->records) || length > 255)
        return fail(b->message, b->message_size, "too many byte patches");
    memcpy(b->records + b->records_size, &offset, sizeof(offset));
    b->records[b->records_size + 4] = (uint8_t)length;
    memcpy(b->records + b->records_size + 5, original, length);
    b->records_size += 5 + length;
    b->patch_count++;
    if (patched != NULL)
        memcpy(b->data + offset, patched, length);
    else
        memset(b->data + offset, 0x90, length);
    return 0;
}

// the patch network_version_patch makes for each of these: the first match within 16 bytes of the address
static int patch_near(build_t *b, const uint32_t *addresses, size_t count, const void *original, const void *patched,
                      uint32_t length) {
    for (size_t i = 0; i < count; i++) {
        int o = 0;
        while (o < 16 && memcmp(b->data + addresses[i] + o, original, length) != 0)
            o++;
        if (o == 16)
            return fail(b->message, b->message_size, "unexpected code near %08x", addresses[i]);
        if (patch(b, addresses[i] + o, original, patched, length) != 0)
            return -1;
    }
    return 0;
}

static void put_rel32(uint8_t *at, uint64_t target, uint64_t next) {
    int32_t rel = (int32_t)((int64_t)target - (int64_t)next);
    memcpy(at, &rel, sizeof(rel));
}
static int code_room(build_t *b,uint32_t *code,uint32_t required) {
    if (*code>=ADDR_Prepatch_Header) {
        if (*code<=ADDR_Prepatch_CodeEnd && required<=ADDR_Prepatch_CodeEnd-*code) return 0;
#if T7O_DEV_NATIVE
        b->tail_code_end=*code;*code=Native_Code_Start;
#else
        return fail(b->message,b->message_size,"out of room for code stubs");
#endif
    }
    if (*code<Native_Code_Start || *code>Native_Code_End || required>Native_Code_End-*code)
        return fail(b->message,b->message_size,"out of room for native code stubs");
    return 0;
}

// checks that the padding and slots BO3MacFix uses are where it expects in this executable
static int check_layout(build_t *b) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)b->data;
    if (b->size < sizeof(*mh) || mh->magic != MH_MAGIC_64 || mh->cputype != CPU_TYPE_X86_64 || mh->filetype != MH_EXECUTE ||
        sizeof(*mh) + mh->sizeofcmds > b->size)
        return fail(b->message, b->message_size, "it isn't the x86_64 game executable");
    uint64_t text_file_end = 0, text_sections_end = 0, text_first_section=UINT64_MAX,
        data_start = 0, data_end = 0, data_sections_end = 0;
    const uint8_t *p = b->data + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmdsize < sizeof(*lc) || p + lc->cmdsize > b->data + sizeof(*mh) + mh->sizeofcmds)
            return fail(b->message, b->message_size, "its load commands are damaged");
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            const struct section_64 *sections = (const struct section_64 *)(seg + 1);
            uint64_t sections_end = 0;
            for (uint32_t s = 0; s < seg->nsects; s++) {
                if (sections[s].addr + sections[s].size > sections_end)
                    sections_end = sections[s].addr + sections[s].size;
            }
            if (strcmp(seg->segname, "__TEXT") == 0 && seg->vmaddr == Game_Base && seg->fileoff == 0) {
                text_file_end = seg->filesize;
                text_sections_end = sections_end - Game_Base;
                for (uint32_t s=0;s<seg->nsects;++s)
                    if (sections[s].size && sections[s].addr-Game_Base<text_first_section)
                        text_first_section=sections[s].addr-Game_Base;
            } else if (strcmp(seg->segname, "__DATA") == 0) {
                data_start = seg->vmaddr - Game_Base;
                data_end = data_start + seg->vmsize;
                data_sections_end = sections_end - Game_Base;
            }
        }
        p += lc->cmdsize;
    }
    if (text_sections_end == 0 || text_sections_end > ADDR_Prepatch_Header || ADDR_Prepatch_CodeEnd > text_file_end ||
        data_sections_end == 0 || data_sections_end > ADDR_Prepatch_Slots || ADDR_Prepatch_SlotsEnd > data_end ||
        ADDR_Prepatch_Slots < data_start)
        return fail(b->message, b->message_size, "it doesn't have room where this BO3MacFix expects it");
    for (uint32_t o = ADDR_Prepatch_Header; o < ADDR_Prepatch_CodeEnd; o++) {
        if (b->data[o] != 0)
            return fail(b->message, b->message_size, "the padding after its code isn't empty");
    }
#if T7O_DEV_NATIVE
    if (sizeof(*mh)+mh->sizeofcmds>Native_Code_Start || text_first_section<Native_Code_End ||
        text_file_end<Native_Code_End)
        return fail(b->message,b->message_size,"native code padding overlaps the Mach-O layout");
    for (uint32_t o=Native_Code_Start;o<Native_Code_End;++o)
        if (b->data[o]) return fail(b->message,b->message_size,"native header padding is not empty");
#endif
    return 0;
}

static int sha256_file(const char *path, uint8_t digest[CC_SHA256_DIGEST_LENGTH], uint8_t **contents, size_t *size) {
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    uint8_t *data = malloc(st.st_size);
    size_t done = 0;
    while (data != NULL && done < (size_t)st.st_size) {
        ssize_t n = read(fd, data + done, st.st_size - done);
        if (n <= 0)
            break;
        done += n;
    }
    close(fd);
    if (data == NULL || done != (size_t)st.st_size) {
        free(data);
        return -1;
    }
    CC_SHA256(data, (CC_LONG)done, digest);
    if (contents != NULL) {
        *contents = data;
        *size = done;
    } else {
        free(data);
    }
    return 0;
}

EXPORT int BO3MacFix_PrepatchedIsCurrent(const char *original, const char *copy, char *message, size_t message_size) {
    prepatch_header_t h;
    FILE *fp = fopen(copy, "rb");
    if (fp == NULL)
        return fail(message, message_size, "there's no pre-patched executable yet");
    bool ok = fseek(fp, ADDR_Prepatch_Header, SEEK_SET) == 0 && fread(&h, sizeof(h), 1, fp) == 1;
    fclose(fp);
    if (!ok || memcmp(h.magic, Prepatch_Magic, sizeof(h.magic)) != 0 || h.version != Prepatch_Version)
        return fail(message, message_size, "the pre-patched executable is from an older BO3MacFix");
    if (h.table_id != table_id())
        return fail(message, message_size, "the pre-patched executable was made by a different BO3MacFix");
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    if (sha256_file(original, digest, NULL, NULL) != 0)
        return fail(message, message_size, "couldn't read %s", original);
    if (memcmp(digest, h.source_sha256, sizeof(digest)) != 0)
        return fail(message, message_size, "the game was updated since the pre-patched executable was made");
    snprintf(message, message_size, "the pre-patched executable is up to date");
    return 0;
}

EXPORT int BO3MacFix_BuildPrepatched(const char *original, const char *output, char *message, size_t message_size) {
    build_t *b = calloc(1, sizeof(build_t));
    if (b == NULL)
        return fail(message, message_size, "out of memory");
    b->message = message;
    b->message_size = message_size;
    prepatch_header_t h = { .version = Prepatch_Version, .table_id = table_id() };
    memcpy(h.magic, Prepatch_Magic, sizeof(h.magic));
    int result = -1;
    const size_t hook_count = sizeof(hook_defs) / sizeof(hook_defs[0]);
    const size_t value_count = sizeof(value_defs) / sizeof(value_defs[0]);
    const size_t call_count = sizeof(call_defs) / sizeof(call_defs[0]);
    prepatch_hook_t hook_records[sizeof(hook_defs) / sizeof(hook_defs[0])];
    prepatch_site_t value_records[sizeof(value_defs) / sizeof(value_defs[0])];
    prepatch_site_t call_records[sizeof(call_defs) / sizeof(call_defs[0])];
    memset(hook_records, 0, sizeof(hook_records));
    memset(value_records, 0, sizeof(value_records));
    memset(call_records, 0, sizeof(call_records));

    if (sha256_file(original, h.source_sha256, &b->data, &b->size) != 0) {
        fail(message, message_size, "couldn't read %s", original);
        goto done;
    }
    if (check_layout(b) != 0)
        goto done;

    // the byte patches
    for (size_t i = 0; i < sizeof(patch_defs) / sizeof(patch_defs[0]); i++) {
        if (patch(b, patch_defs[i].offset, patch_defs[i].original, patch_defs[i].patched, patch_defs[i].length) != 0)
            goto done;
    }
    if (patch_near(b, netver_addresses, sizeof(netver_addresses) / sizeof(netver_addresses[0]), &netver_original,
                   &netver_patched, sizeof(netver_original)) != 0 ||
        patch_near(b, tuver_addresses, sizeof(tuver_addresses) / sizeof(tuver_addresses[0]), tuver_original,
                   tuver_patched, sizeof(tuver_original)) != 0)
        goto done;

    // the header and its records come first in the padding, then the stubs
    h.hook_count = (uint32_t)hook_count;
    h.value_count = (uint32_t)value_count;
    h.call_count = (uint32_t)call_count;
    h.patch_count = b->patch_count;
    h.hooks_offset = ADDR_Prepatch_Header + sizeof(h);
    h.values_offset = h.hooks_offset + sizeof(hook_records);
    h.calls_offset = h.values_offset + sizeof(value_records);
    h.patches_offset = h.calls_offset + sizeof(call_records);
    uint32_t header_end = h.patches_offset + (uint32_t)b->records_size;
    uint32_t code = (header_end + 15) & ~15u, slot = ADDR_Prepatch_Slots;
    if (claim(b, ADDR_Prepatch_Header, header_end - ADDR_Prepatch_Header, "the header") != 0)
        goto done;

    for (size_t i = 0; i < hook_count; i++) {
        if (code_room(b,&code,64)) goto done;
        const hook_def_t *def = &hook_defs[i];
        uint32_t f = def->function, tramp = code + 16;
        if (def->length < 5 || def->length>sizeof(def->stolen) ||
            memcmp(b->data + f, def->stolen, def->length) != 0) {
            fail(message, message_size, "unexpected code at %08x", f);
            goto done;
        }
        uint8_t stub[64];
        size_t n = 0;
        stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0x3D;               // cmp qword [rip+slot], 0
        put_rel32(stub + n, slot, code + 8); n += 4;
        stub[n++] = 0x00;
        stub[n++] = 0x74; stub[n++] = 0x06;                                  // je trampoline
        stub[n++] = 0xFF; stub[n++] = 0x25;                                  // jmp [rip+slot]
        put_rel32(stub + n, slot, code + 16); n += 4;
        if (def->short_jcc_at != 0) {
            // the instructions before it, then the same condition as a near jump to the same target
            size_t at = def->short_jcc_at - 1;
            uint64_t target = f + at + 2 + (int8_t)def->stolen[at + 1];
            memcpy(stub + n, def->stolen, at); n += at;
            stub[n++] = 0x0F; stub[n++] = 0x80 | (def->stolen[at] & 0x0F);
            put_rel32(stub + n, target, code + n + 4); n += 4;
            memcpy(stub + n, def->stolen + at + 2, def->length - at - 2); n += def->length - at - 2;
        } else {
            memcpy(stub + n, def->stolen, def->length);
            if (f==T7O_MAC_CLIENT_CAN_PAUSE &&
                !t7o_relocate_rip32(stub+n,def->length,7,f+11,code+n+11)) {
                fail(message,message_size,"client pause trampoline does not fit");goto done;
            }
            if ((f==T7O_MAC_STRAFE_SCALE || f==T7O_MAC_FRICTION_SCALE) &&
                !t7o_relocate_rip32(stub+n,def->length,8,f+12,code+n+12)) {
                fail(message,message_size,"strafe trampoline does not fit");goto done;
            }
            if (f==T7O_MAC_MIGRATION_CHECK &&
                !t7o_relocate_rip32(stub+n,def->length,5,f+9,code+n+9)) {
                fail(message,message_size,"migration call trampoline does not fit");goto done;
            }
            if (f==T7O_MAC_SESSION_IS_MODE &&
                !t7o_relocate_rip32(stub+n,def->length,6,f+10,code+n+10)) {
                fail(message,message_size,"session mode trampoline does not fit");goto done;
            }
            n += def->length;
        }
        stub[n++] = 0xE9;                                                    // jmp back
        put_rel32(stub + n, f + def->length, code + n + 4); n += 4;
        if (claim(b, code, (uint32_t)n, "a hook stub") != 0 || claim(b, f, def->length, "a hook") != 0)
            goto done;
        memcpy(b->data + code, stub, n);
        b->data[f] = 0xE9;
        put_rel32(b->data + f + 1, code, f + 5);
        memset(b->data + f + 5, 0x90, def->length - 5);
        hook_records[i] = (prepatch_hook_t){ .function = f, .slot = slot, .trampoline = tramp, .length = def->length };
        slot += 8;
        /* Slot pointers retain their eight-byte alignment in __DATA. */
        code = (code + (uint32_t)n + Hook_Code_Alignment-1) & ~(Hook_Code_Alignment-1);
    }

    for (size_t i = 0; i < value_count; i++) {
        if (code_room(b,&code,64)) goto done;
        const value_def_t *def = &value_defs[i];
        uint32_t site = def->site;
        if (def->kind!=0) {
            size_t length=def->length;
            if (length>sizeof(def->original) || length<5 || memcmp(b->data+site,def->original,length)) {
                fail(message,message_size,"unexpected native setting code at %08x",site);goto done;
            }
            uint8_t stub[64];size_t n=0;
            if (def->kind==1) n=t7o_codegen_cmp32(stub,sizeof(stub),code,site,slot,def->original,length);
            else if (def->kind==4) n=t7o_codegen_ads_scale(stub,sizeof(stub),code,site,slot,def->original);
            else if (def->kind==5 || def->kind==6)
                n=t7o_codegen_inline(stub,sizeof(stub),code,site,slot,def->original,length,def->kind==6);
            else if (def->kind==7) n=t7o_codegen_float(stub,sizeof(stub),code,site,slot,def->original);
            else n=t7o_codegen_branch(stub,sizeof(stub),code,site,slot,def->original,length,def->kind==2);
            if (!n || claim(b,code,(uint32_t)n,"a native setting stub") || claim(b,site,(uint32_t)length,"a native setting")) {
                fail(message,message_size,"native setting stub failed at %08x",site);goto done;
            }
            memcpy(b->data+code,stub,n);b->data[site]=0xe9;
            put_rel32(b->data+site+1,code,site+5);memset(b->data+site+5,0x90,length-5);
            value_records[i]=(prepatch_site_t){.site=site,.slot=slot,.length=(uint8_t)length};
            value_records[i].pad[0]=def->kind;
            slot+=8;code=(code+(uint32_t)n+Hook_Code_Alignment-1)&~(Hook_Code_Alignment-1);continue;
        }
        uint8_t reg = def->original[0] - 0xB8;
        if (memcmp(b->data + site, def->original, 5) != 0 || def->original[0] < 0xB8 || def->original[0] > 0xBF) {
            fail(message, message_size, "unexpected code at %08x", site);
            goto done;
        }
        uint8_t stub[32];
        size_t n = 0;
        stub[n++] = 0x8B; stub[n++] = 0x05 | (reg << 3);                     // mov r32, [rip+slot]
        put_rel32(stub + n, slot, code + 6); n += 4;
        stub[n++] = 0x85; stub[n++] = 0xC0 | (reg << 3) | reg;               // test r32, r32
        stub[n++] = 0x75; stub[n++] = 0x05;                                  // jnz past the original
        memcpy(stub + n, def->original, 5); n += 5;                          // mov r32, original value
        stub[n++] = 0xE9;                                                    // jmp back
        put_rel32(stub + n, site + 5, code + n + 4); n += 4;
        if (claim(b, code, (uint32_t)n, "a setting stub") != 0 || claim(b, site, 5, "a setting") != 0)
            goto done;
        memcpy(b->data + code, stub, n);
        b->data[site] = 0xE9;
        put_rel32(b->data + site + 1, code, site + 5);
        value_records[i] = (prepatch_site_t){ .site = site, .slot = slot, .length = 5 };
        slot += 8;
        code = (code + (uint32_t)n + Hook_Code_Alignment-1) & ~(Hook_Code_Alignment-1);
    }

    for (size_t i = 0; i < call_count; i++) {
        if (code_room(b,&code,32)) goto done;
        const call_def_t *def = &call_defs[i];
        uint32_t site = def->site;
        if (memcmp(b->data + site, def->original, 6) != 0) {
            fail(message, message_size, "unexpected code at %08x", site);
            goto done;
        }
        int32_t disp;
        memcpy(&disp, def->original + 2, sizeof(disp));
        uint64_t pointer = site + 6 + (int64_t)disp; // where the call reads objc_msgSend's address from
        uint8_t stub[32];
        size_t n = 0;
        stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0x3D;               // cmp qword [rip+slot], 0
        put_rel32(stub + n, slot, code + 8); n += 4;
        stub[n++] = 0x00;
        stub[n++] = 0x74; stub[n++] = 0x06;                                  // je the original call
        stub[n++] = 0xFF; stub[n++] = 0x25;                                  // jmp [rip+slot]
        put_rel32(stub + n, slot, code + 16); n += 4;
        stub[n++] = 0xFF; stub[n++] = 0x25;                                  // jmp [rip+objc_msgSend pointer]
        put_rel32(stub + n, pointer, code + 22); n += 4;
        if (claim(b, code, (uint32_t)n, "a call stub") != 0 || claim(b, site, 6, "a call") != 0)
            goto done;
        memcpy(b->data + code, stub, n);
        b->data[site] = 0xE8;                                                // call stub; nop
        put_rel32(b->data + site + 1, code, site + 5);
        b->data[site + 5] = 0x90;
        call_records[i] = (prepatch_site_t){ .site = site, .slot = slot, .length = 6 };
        slot += 8;
        code = (code + (uint32_t)n + Hook_Code_Alignment-1) & ~(Hook_Code_Alignment-1);
    }

    if (slot > ADDR_Prepatch_SlotsEnd) {
        fail(message, message_size, "out of room for the stubs");
        goto done;
    }
    memcpy(b->data + ADDR_Prepatch_Header, &h, sizeof(h));
    memcpy(b->data + h.hooks_offset, hook_records, sizeof(hook_records));
    memcpy(b->data + h.values_offset, value_records, sizeof(value_records));
    memcpy(b->data + h.calls_offset, call_records, sizeof(call_records));
    memcpy(b->data + h.patches_offset, b->records, b->records_size);

    int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        fail(message, message_size, "couldn't create %s", output);
        goto done;
    }
    size_t written = 0;
    while (written < b->size) {
        ssize_t n = write(fd, b->data + written, b->size - written);
        if (n <= 0)
            break;
        written += n;
    }
    fchmod(fd, 0755);
    if (close(fd) != 0 || written != b->size) {
        unlink(output);
        fail(message, message_size, "couldn't write %s", output);
        goto done;
    }
    snprintf(message, message_size, "made the pre-patched executable: %zu hooks, %zu settings, %zu calls, %u byte "
             "patches, %u of %u bytes of padding used", hook_count, value_count, call_count, b->patch_count,
             b->tail_code_end ? b->tail_code_end-ADDR_Prepatch_Header+code-Native_Code_Start : code-ADDR_Prepatch_Header,
             ADDR_Prepatch_CodeEnd-ADDR_Prepatch_Header+(T7O_DEV_NATIVE ? Native_Code_End-Native_Code_Start : 0));
    result = 0;
done:
    free(b->data);
    free(b);
    return result;
}
