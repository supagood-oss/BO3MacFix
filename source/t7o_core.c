#include "t7o_core.h"
#include <math.h>
int t7o_traversal_type(unsigned flags,float height,float offset) {
    if (!isfinite(offset)) offset=-18.0f;
    if (offset< -45.0f) offset=-45.0f;
    if (offset>512.0f) offset=512.0f;
    if (height>=96.0f+offset) {
        if (!(flags&1)) return height>=350.0f+offset ? 21 : 20;
        if (height<200.0f+offset) return 17;
        return height>=350.0f+offset ? 19 : 18;
    }
    /* The native wrapper falls through to the engine's lower-height branches.
     * Preserve all flags there, including the original 1/50-unit thresholds. */
    if (flags&1) {
        if (flags&2) return 15+((flags>>2)&1);
        if (flags&16) return 14;
        return 13-((flags>>3)&1);
    }
    if (flags&2) return height<1.0f ? 3+((flags>>2)&1) :
                          height<50.0f ? 7+((flags>>2)&1) : 10+((flags>>2)&1);
    if (flags&16) return height>=1.0f ? 6 : 2;
    return height<1.0f ? 1 : height<50.0f ? 5 : 9;
}
int t7o_aim_status_tail(int initial,bool holding,int32_t spent,int32_t limit,int32_t recovery,int32_t frame_ms) {
    if (recovery>0) recovery=(int32_t)((uint32_t)recovery-(uint32_t)frame_ms);
    if (holding) {
        if (spent>=0) return spent<=limit ? 3 : 9;
        return recovery<=0 ? 6 : initial;
    }
    if (spent>=0) {
        if ((int32_t)((uint32_t)spent+(uint32_t)frame_ms)>4500) return 8;
        if (recovery<=0) return 7;
    }
    return initial;
}
#include <stdlib.h>
#include <string.h>
#include <limits.h>

bool t7o_relocate_rip32(uint8_t *bytes,size_t length,size_t offset,
    uint64_t original_next,uint64_t relocated_next) {
    if (!bytes || offset>length || length-offset<4) return false;
    int32_t old;memcpy(&old,bytes+offset,4);
    uint64_t target;
    if (old<0) {
        uint64_t distance=(uint64_t)(-(int64_t)old);
        if (original_next<distance) return false;
        target=original_next-distance;
    } else {
        if (original_next>UINT64_MAX-(uint32_t)old) return false;
        target=original_next+(uint32_t)old;
    }
    int64_t delta;
    if (target>=relocated_next) {
        uint64_t distance=target-relocated_next;
        if (distance>INT32_MAX) return false;
        delta=(int64_t)distance;
    } else {
        uint64_t distance=relocated_next-target;
        if (distance>UINT64_C(2147483648)) return false;
        delta=-(int64_t)distance;
    }
    int32_t relocated=(int32_t)delta;memcpy(bytes+offset,&relocated,4);return true;
}

uint32_t t7o_script_hash(const char *text) {
    uint32_t hash = UINT32_C(0x4b9ace2f);
    if (text == NULL) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        hash = (hash ^ c) * UINT32_C(0x01000193);
    }
    return hash * UINT32_C(0x01000193); /* Include the trailing NUL. */
}

static const char *const movement_names[T7O_MOVEMENT_COUNT] = {
    "slide_preserve_momentum", "jumpslowdown", "fast_sprintleap",
    "fast_sprintlerpin", "slideslowdown", "canslidethensprint",
    "instant_slide", "fastswap", "fastads", "fastjump", "slideangles"
};

const char *t7o_movement_name(T7oMovementPatch patch) {
    return (unsigned)patch < T7O_MOVEMENT_COUNT ? movement_names[patch] : NULL;
}

bool t7o_movement_find(const char *name, T7oMovementPatch *patch) {
    if (name == NULL || patch == NULL) return false;
    for (unsigned i = 0; i < T7O_MOVEMENT_COUNT; ++i) {
        if (strcmp(name, movement_names[i]) == 0) {
            *patch = (T7oMovementPatch)i;
            return true;
        }
    }
    return false;
}

void t7o_movement_defaults(bool aae, bool enabled[T7O_MOVEMENT_COUNT]) {
    for (unsigned i = 0; i < T7O_MOVEMENT_COUNT; ++i) enabled[i] = aae;
    enabled[T7O_FAST_SWAP] = false;
}

void t7o_omni_sprint(int8_t forward, int8_t right, uint32_t buttons,
                    int8_t *adjusted_forward, uint32_t *adjusted_buttons) {
    int f = forward, r = right;
    int magnitude = abs(f) > abs(r) ? abs(f) : abs(r);
    if (magnitude > 127) magnitude = 127;
    *adjusted_forward = (int8_t)magnitude;
    *adjusted_buttons = buttons;
    if ((buttons & (UINT32_C(1) << 18)) != 0 && magnitude > 0)
        *adjusted_buttons |= UINT32_C(1) << 30;
}

float t7o_strafe_scale(float configured) {
    /* Same baseline constant as the AAE DLL (float bits 0x3f2ac083). */
    const float baseline = 0.667f;
    return isfinite(configured) && configured > baseline ? configured : baseline;
}

T7oPatchResult t7o_patch_transaction(T7oBytePatch *patches, size_t count, bool enable,
    T7oReadMemory read_memory, T7oWriteMemory write_memory, void *context) {
    if ((patches == NULL && count != 0) || read_memory == NULL || write_memory == NULL)
        return T7O_PATCH_INVALID;
    /* Reject overlap, including addresses that overflow. */
    for (size_t i = 0; i < count; ++i) {
        T7oBytePatch *p = &patches[i];
        if (p->length == 0 || p->length > sizeof(p->original) ||
            p->address > UINTPTR_MAX - p->length) return T7O_PATCH_INVALID;
        for (size_t j = 0; j < i; ++j)
            if (p->address < patches[j].address + patches[j].length &&
                patches[j].address < p->address + p->length) return T7O_PATCH_INVALID;
    }
    for (size_t i = 0; i < count; ++i) {
        T7oBytePatch *p = &patches[i];
        uint8_t actual[16];
        if (!read_memory(context, p->address, actual, p->length)) return T7O_PATCH_READ_FAILED;
        const uint8_t *expected = p->enabled ? p->replacement : p->original;
        if (memcmp(actual, expected, p->length) != 0) return T7O_PATCH_MISMATCH;
    }
    /* State flags are committed together after every write succeeds. */
    for (size_t i = 0; i < count; ++i) {
        T7oBytePatch *p = &patches[i];
        if (p->enabled == enable) continue;
        if (!write_memory(context, p->address, enable ? p->replacement : p->original, p->length)) {
            bool restored = true;
            while (i > 0) {
                T7oBytePatch *old = &patches[--i];
                if (old->enabled != enable && !write_memory(context, old->address,
                    old->enabled ? old->replacement : old->original, old->length)) restored = false;
            }
            return restored ? T7O_PATCH_WRITE_FAILED : T7O_PATCH_ROLLBACK_FAILED;
        }
    }
    for (size_t i = 0; i < count; ++i) patches[i].enabled = enable;
    return T7O_PATCH_OK;
}

bool t7o_pool_extend(T7oAssetPool *pool, int new_count, void **allocation) {
    if (allocation == NULL) return false;
    *allocation = NULL;
    if (pool == NULL || pool->singleton || pool->item_size < sizeof(void *) ||
        pool->item_count < 0 || pool->item_alloc_count < 0 ||
        pool->item_alloc_count > pool->item_count || new_count < 0 || new_count > 100000)
        return false;
    if (new_count <= pool->item_count) return true;
    size_t added = (size_t)(new_count - pool->item_count);
    if (added > SIZE_MAX / pool->item_size || pool->item_size % _Alignof(void *) != 0)
        return false;
    uint8_t *block = calloc(added, pool->item_size);
    if (block == NULL) return false;
    for (size_t i = 0; i < added; ++i) {
        void *next = i + 1 < added ? block + (i + 1) * pool->item_size : pool->free_head;
        memcpy(block + i * pool->item_size, &next, sizeof(next));
    }
    pool->free_head = block;
    pool->item_count = new_count;
    *allocation = block;
    return true;
}

bool t7o_fx_prepare(T7oFxStorage *storage, const void *original[2], size_t old_capacity, size_t new_capacity) {
    if (!storage || !original || !original[0] || !original[1] || storage->bound ||
        storage->entries[0] || storage->entries[1] || !old_capacity ||
        new_capacity < old_capacity || new_capacity > 2048) return false;
    uint8_t *first=calloc(new_capacity,16), *second=calloc(new_capacity,16);
    if (!first || !second) { free(first);free(second);return false; }
    memcpy(first,original[0],old_capacity*16);
    memcpy(second,original[1],old_capacity*16);
    storage->entries[0]=first;storage->entries[1]=second;storage->capacity=new_capacity;
    return true;
}
void *t7o_fx_slot(T7oFxStorage *storage, int instance, int index) {
    if (!storage || instance<0 || instance>1 || index<0 || (size_t)index>=storage->capacity ||
        !storage->entries[instance]) return NULL;
    return storage->entries[instance]+(size_t)index*16;
}
bool t7o_fx_clear(T7oFxStorage *storage, int instance) {
    if (!storage || instance<0 || instance>1 || !storage->entries[instance]) return false;
    memset(storage->entries[instance],0,storage->capacity*16);
    return true;
}
bool t7o_fx_discard(T7oFxStorage *storage) {
    if (!storage || storage->bound) return false;
    free(storage->entries[0]);free(storage->entries[1]);
    memset(storage,0,sizeof(*storage));return true;
}

T7oScriptResult t7o_script_dispatch(const T7oScriptBackend *b, int instance) {
    if (b == NULL || b->get_int == NULL || instance < 0 || instance > 1)
        return T7O_SCRIPT_BAD_ARGUMENT;
    int32_t operation, a, c, d;
    if (!b->get_int(b->context, instance, 0, &operation)) return T7O_SCRIPT_BAD_ARGUMENT;
    uint32_t id = (uint32_t)operation;
    if (id == UINT32_C(0x22ea46c7)) {
        if (b->get_string == NULL || b->push_int == NULL) return T7O_SCRIPT_UNSUPPORTED;
        const char *text = b->get_string(b->context, instance, 1);
        if (text == NULL) return T7O_SCRIPT_BAD_ARGUMENT;
        return b->push_int(b->context, instance, (int32_t)t7o_script_hash(text)) ?
            T7O_SCRIPT_OK : T7O_SCRIPT_ENGINE_FAILED;
    }
    if (id != UINT32_C(0x35224a7a) && id != UINT32_C(0x50ee1cfb) &&
        id != UINT32_C(0xe2a60154) && id != UINT32_C(0xe2555348)) return T7O_SCRIPT_UNSUPPORTED;
    if (!b->get_int(b->context, instance, 1, &a)) return T7O_SCRIPT_BAD_ARGUMENT;
    if (id == UINT32_C(0x35224a7a)) {
        if (b->get_view_origin == NULL || b->push_vector == NULL) return T7O_SCRIPT_UNSUPPORTED;
        float vector[3];
        if (!b->get_view_origin(b->context, instance, a, vector)) return T7O_SCRIPT_ENGINE_FAILED;
        return b->push_vector(b->context, instance, vector) ? T7O_SCRIPT_OK : T7O_SCRIPT_ENGINE_FAILED;
    }
    if (!b->get_int(b->context, instance, 2, &c)) return T7O_SCRIPT_BAD_ARGUMENT;
    if (id == UINT32_C(0x50ee1cfb)) {
        if (b->animation_event == NULL) return T7O_SCRIPT_UNSUPPORTED;
        return b->animation_event(b->context, instance, a, c) ? T7O_SCRIPT_OK : T7O_SCRIPT_ENGINE_FAILED;
    }
    if (!b->get_int(b->context, instance, 3, &d)) return T7O_SCRIPT_BAD_ARGUMENT;
    if (id == UINT32_C(0xe2a60154)) {
        if (b->start_weapon_anim == NULL) return T7O_SCRIPT_UNSUPPORTED;
        return b->start_weapon_anim(b->context, instance, a, c, d) ? T7O_SCRIPT_OK : T7O_SCRIPT_ENGINE_FAILED;
    }
    if (b->play_anim == NULL) return T7O_SCRIPT_UNSUPPORTED;
    return b->play_anim(b->context, instance, a, c, d) ? T7O_SCRIPT_OK : T7O_SCRIPT_ENGINE_FAILED;
}
