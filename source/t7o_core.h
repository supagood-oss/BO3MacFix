#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* AAE traversal animation selection for finite relative heights. The high
 * offset uses the native -45..512 range, with -18 as the default. */
int t7o_traversal_type(unsigned flags,float relative_height,float high_offset);

/* Platform-independent behavior recovered from the current mod DLLs.
 * These functions do not locate or patch engine memory. Mac bindings must be
 * verified independently before the corresponding capability is advertised. */

uint32_t t7o_script_hash(const char *text);

typedef enum {
    T7O_SLIDE_MOMENTUM, T7O_JUMP_SLOWDOWN, T7O_SPRINT_LEAP,
    T7O_SPRINT_LERP, T7O_SLIDE_SLOWDOWN, T7O_SLIDE_THEN_SPRINT,
    T7O_INSTANT_SLIDE, T7O_FAST_SWAP, T7O_FAST_ADS, T7O_FAST_JUMP,
    T7O_SLIDE_ANGLES, T7O_MOVEMENT_COUNT
} T7oMovementPatch;

const char *t7o_movement_name(T7oMovementPatch patch);
bool t7o_movement_find(const char *name, T7oMovementPatch *patch);
void t7o_movement_defaults(bool aae, bool enabled[T7O_MOVEMENT_COUNT]);

/* Values are supplied by a verified adapter, not by casting a Mac usercmd to
 * a Windows layout. The caller restores both values after the original call. */
void t7o_omni_sprint(int8_t forward, int8_t right, uint32_t buttons,
                    int8_t *adjusted_forward, uint32_t *adjusted_buttons);
float t7o_strafe_scale(float configured);
/* Native aim/breath UI state. Addition/subtraction use wrapping int32 engine
 * arithmetic; the extra recovery states extend the older 0..4 enum. */
int t7o_aim_status_tail(int initial,bool holding,int32_t spent,int32_t limit,int32_t recovery,int32_t frame_ms);
/* Relocate a known x86 RIP-relative disp32 after its instruction is copied.
 * No instruction decoding is inferred. Leaves bytes intact on overflow. */
bool t7o_relocate_rip32(uint8_t *bytes,size_t length,size_t displacement_offset,
    uint64_t original_next,uint64_t relocated_next);

typedef bool (*T7oReadMemory)(void *context, uintptr_t address, void *bytes, size_t length);
typedef bool (*T7oWriteMemory)(void *context, uintptr_t address, const void *bytes, size_t length);
typedef struct {
    uintptr_t address;
    size_t length;
    uint8_t original[16];
    uint8_t replacement[16];
    bool enabled;
} T7oBytePatch;

typedef enum {
    T7O_PATCH_OK, T7O_PATCH_INVALID, T7O_PATCH_MISMATCH,
    T7O_PATCH_READ_FAILED, T7O_PATCH_WRITE_FAILED, T7O_PATCH_ROLLBACK_FAILED
} T7oPatchResult;

/* Call on the game thread with other users of the patch region stopped.
 * Every site is checked before any write. A partial write is rolled back.
 * A backend must make each individual write atomic or leave its site intact
 * on failure. Rosetta prepatches should use slot changes instead of code writes. */
T7oPatchResult t7o_patch_transaction(T7oBytePatch *patches, size_t count, bool enable,
    T7oReadMemory read_memory, T7oWriteMemory write_memory, void *context);

typedef struct {
    void *pool;
    uint32_t item_size;
    int32_t item_count;
    bool singleton;
    int32_t item_alloc_count;
    void *free_head;
} T7oAssetPool;

/* The returned allocation belongs to the game pool for the process lifetime.
 * Do not release it during mod/VM teardown while engine references may exist. */
bool t7o_pool_extend(T7oAssetPool *pool, int new_count, void **allocation);

/* BGCache storage is separate from DB asset pools. Preserve all 16 bytes of
 * each existing record; only a qualified adapter may interpret its fields. */
typedef struct {
    uint8_t *entries[2];
    size_t capacity;
    bool bound;
} T7oFxStorage;
bool t7o_fx_prepare(T7oFxStorage *storage, const void *original[2], size_t old_capacity, size_t new_capacity);
void *t7o_fx_slot(T7oFxStorage *storage, int instance, int index);
bool t7o_fx_clear(T7oFxStorage *storage, int instance);
bool t7o_fx_discard(T7oFxStorage *storage);

typedef struct {
    void *context;
    bool (*get_int)(void *, int instance, unsigned index, int32_t *value);
    const char *(*get_string)(void *, int instance, unsigned index);
    bool (*push_int)(void *, int instance, int32_t value);
    bool (*push_vector)(void *, int instance, const float vector[3]);
    bool (*get_view_origin)(void *, int instance, int local_client, float vector[3]);
    bool (*animation_event)(void *, int instance, int entity, int event);
    bool (*start_weapon_anim)(void *, int instance, int local_client, int anim, int hand);
    bool (*play_anim)(void *, int instance, int local_client, int anim, int flags);
} T7oScriptBackend;

typedef enum { T7O_SCRIPT_OK, T7O_SCRIPT_BAD_ARGUMENT, T7O_SCRIPT_UNSUPPORTED,
               T7O_SCRIPT_ENGINE_FAILED } T7oScriptResult;

/* IsProfileBuild bridge: operation ID is argument 0; operation parameters
 * start at 1. The unrelated normal int builtin must retain its semantics. */
T7oScriptResult t7o_script_dispatch(const T7oScriptBackend *backend, int instance);
