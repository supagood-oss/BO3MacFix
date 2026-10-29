#include "t7o_mac_checkpoint.h"
#include "t7o_mac_offsets.h"
#include "prepatch.h"
#include "dobby.h"
#include <stdatomic.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static void (*original_restore)(void *,uintptr_t);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uintptr_t read_pointer(const void *p) { uintptr_t v;memcpy(&v,p,8);return v; }
static void write_pointer(void *p,uintptr_t v) { memcpy(p,&v,8); }
enum { POOL_SIZE=0x19000,POOL_USED=0x18ff0,POOL_OFFSET=0x22c428,SENTINEL=0x2d60 };
static uintptr_t relocated(uintptr_t value,uintptr_t old_pool,uintptr_t new_pool,
                           uintptr_t old_sentinel,uintptr_t new_sentinel) {
    return value==old_sentinel ? new_sentinel : value-old_pool+new_pool;
}
/* Called only after the engine has rebound fx->8a0 to its current state buffer.
 * Validate the complete block partition and doubly linked free list before any
 * write. The Mac initializer confirms the same pool, sentinel and size layout. */
static bool relocate_fx(uint8_t *fx,uintptr_t delta) {
    if (!fx || read32(fx+0x2d80)!=POOL_SIZE) return false;
    uintptr_t old_pool=read_pointer(fx+0x2d78),state=read_pointer(fx+0x8a0);
    if (!old_pool || old_pool>UINTPTR_MAX-POOL_SIZE || !state || state>UINTPTR_MAX-POOL_OFFSET-POOL_SIZE) return false;
    uint8_t *pool=(void *)(state+POOL_OFFSET);uintptr_t new_pool=(uintptr_t)pool;
    unsigned offset=0,free_count=0;
    while (offset<POOL_USED) {
        unsigned size=read32(pool+offset);
        if (size<16 || (size&3) || size>POOL_USED-offset) return false;
        if (read_pointer(pool+offset+8)) { if (size<24) return false;++free_count; }
        offset+=size;
    }
    uintptr_t old_sentinel=(uintptr_t)fx-delta+SENTINEL,new_sentinel=(uintptr_t)fx+SENTINEL;
    uintptr_t current=read_pointer(fx+SENTINEL+8),previous=old_sentinel;
    unsigned seen=0;
    while (current!=old_sentinel) {
        if (current<old_pool || current-old_pool>POOL_USED-24 || ++seen>free_count) return false;
        unsigned target=(unsigned)(current-old_pool),block=0;
        while (block<target) block+=read32(pool+block);
        if (block!=target || read_pointer(pool+target+16)!=previous) return false;
        previous=current;current=read_pointer(pool+target+8);
    }
    if (seen!=free_count || read_pointer(fx+SENTINEL+16)!=previous) return false;
    if (!delta && old_pool==new_pool) return false;
    for (offset=0;offset<POOL_USED;offset+=read32(pool+offset)) {
        if (!read_pointer(pool+offset+8)) continue;
        for (unsigned field=8;field<=16;field+=8)
            write_pointer(pool+offset+field,relocated(read_pointer(pool+offset+field),old_pool,new_pool,old_sentinel,new_sentinel));
    }
    for (unsigned field=8;field<=16;field+=8)
        write_pointer(fx+SENTINEL+field,relocated(read_pointer(fx+SENTINEL+field),old_pool,new_pool,old_sentinel,new_sentinel));
    write_pointer(fx+0x2d78,new_pool);return true;
}
static void restore_for(void *fx,uintptr_t delta,uintptr_t caller) {
    original_restore(fx,delta);
    if (atomic_load(&active) && caller==base+T7O_MAC_FX_CHECKPOINT_RESTORE_RETURN &&
        (read32(address(T7O_MAC_SESSION_FLAGS))&15)==0) relocate_fx(fx,delta);
}
static void restore(void *fx,uintptr_t delta) { restore_for(fx,delta,(uintptr_t)__builtin_return_address(0)); }
void t7o_mac_checkpoint_deactivate(void) { atomic_store(&active,false); }
bool t7o_mac_checkpoint_activate(void) {
    if (!installed) return false;
    atomic_store(&active,true);return true;
}
bool t7o_mac_checkpoint_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x48,0x85,0xf6};
    if (memcmp(prepatch_original_code(address(T7O_MAC_FX_CHECKPOINT_RESTORE),sizeof(prefix)),prefix,sizeof(prefix))) return false;
    if (DobbyHook(address(T7O_MAC_FX_CHECKPOINT_RESTORE),(void *)restore,(void **)&original_restore)!=0) return false;
    installed=true;return true;
}
