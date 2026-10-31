#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define T7O_DEV_NATIVE 1
static void *test_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#include "../source/t7o_mac_checkpoint.c"
bool t7o_mac_checkpoint_disk_install(uint64_t b) { (void)b;return true; }
bool t7o_mac_checkpoint_disk_activate(void) { return true; }
void t7o_mac_checkpoint_disk_deactivate(void) { }
void t7o_mac_checkpoint_disk_tick(void) { }
void t7o_mac_checkpoint_disk_frame_begin(int c) { (void)c; }
static uint8_t fx[0x2d90],state[POOL_OFFSET+POOL_SIZE],saved_fx[sizeof(fx)],saved_pool[POOL_SIZE];
static uint32_t session;
static unsigned originals;
static CheckpointBuiltin savegame_row;
static void savegame_mock(unsigned i) { (void)i;abort(); }
static uintptr_t old_pool=UINT64_C(0x2000000000),delta=0x300000;
static void *test_address(uint32_t rva) {
    if (rva==T7O_MAC_SESSION_FLAGS) return &session;
    if (rva==T7O_MAC_SAVEGAME_CREATE_ROW) return &savegame_row;
    if (rva==T7O_MAC_SAVEGAME_CREATE) return (void *)savegame_mock;
    abort();
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
static void original_mock(void *p,uintptr_t d) { assert(p==fx && d==delta);++originals; }
static void fixture(void) {
    memset(fx,0,sizeof(fx));memset(state,0,sizeof(state));session=0;
    write_pointer(fx+0x8a0,(uintptr_t)state);write_pointer(fx+0x2d78,old_pool);put32(fx+0x2d80,POOL_SIZE);
    uint8_t *pool=state+POOL_OFFSET;uintptr_t sentinel=(uintptr_t)fx-delta+SENTINEL;
    put32(pool,32);put32(pool+32,20);put32(pool+52,28);put32(pool+80,16);put32(pool+96,POOL_USED-96);
    /* Deliberately unordered links: the list and physical block order differ. */
    write_pointer(fx+SENTINEL+8,old_pool+96);write_pointer(fx+SENTINEL+16,old_pool+52);
    write_pointer(pool+96+8,old_pool);write_pointer(pool+96+16,sentinel);
    write_pointer(pool+8,old_pool+52);write_pointer(pool+16,old_pool+96);
    write_pointer(pool+52+8,sentinel);write_pointer(pool+52+16,old_pool);
}
static void snapshot(void) { memcpy(saved_fx,fx,sizeof(fx));memcpy(saved_pool,state+POOL_OFFSET,POOL_SIZE); }
static void unchanged(void) { assert(!memcmp(saved_fx,fx,sizeof(fx)) && !memcmp(saved_pool,state+POOL_OFFSET,POOL_SIZE)); }
int main(void) {
    assert(!t7o_mac_checkpoint_install(0,false) && !t7o_mac_checkpoint_activate());
    savegame_row=(CheckpointBuiltin){.hash=t7o_script_hash("savegame_create"),.max_args=1,.function=savegame_mock};
    installed=true;assert(t7o_mac_checkpoint_activate());original_restore=original_mock;base=0x100000000;
    fixture();assert(relocate_fx(fx,delta));uintptr_t pool=(uintptr_t)state+POOL_OFFSET,sentinel=(uintptr_t)fx+SENTINEL;
    assert(read_pointer(fx+0x2d78)==pool && read_pointer(fx+SENTINEL+8)==pool+96 && read_pointer(fx+SENTINEL+16)==pool+52);
    assert(read_pointer((void *)(pool+96+16))==sentinel && read_pointer((void *)(pool+52+8))==sentinel);
    assert(read_pointer((void *)(pool+8))==pool+52 && read_pointer((void *)(pool+16))==pool+96);
    snapshot();assert(!relocate_fx(fx,0));unchanged();
    for (unsigned bad=0;bad<10;++bad) {
        fixture();uint8_t *p=state+POOL_OFFSET;
        switch (bad) {
        case 0:put32(p,0);break;
        case 1:put32(p,31);break;
        case 2:put32(p+96,POOL_USED);break;
        case 3:write_pointer(p+52+16,old_pool+96);break;
        case 4:write_pointer(p+52+8,old_pool);break;
        case 5:write_pointer(fx+SENTINEL+8,old_pool+4);break;
        case 6:write_pointer(fx+SENTINEL+16,old_pool);break;
        case 7:write_pointer(fx+0x2d78,UINTPTR_MAX-8);break;
        case 8:put32(fx+0x2d80,POOL_SIZE-4);break;
        case 9:put32(p,16);break;
        }
        snapshot();assert(!relocate_fx(fx,delta));unchanged();
    }
    fixture();memset(state+POOL_OFFSET,0,POOL_SIZE);put32(state+POOL_OFFSET,POOL_USED);
    sentinel=(uintptr_t)fx-delta+SENTINEL;write_pointer(fx+SENTINEL+8,sentinel);write_pointer(fx+SENTINEL+16,sentinel);
    assert(relocate_fx(fx,delta) && read_pointer(fx+SENTINEL+8)==(uintptr_t)fx+SENTINEL);
    for (unsigned mode=0;mode<3;++mode) {
        fixture();session=mode;snapshot();restore_for(fx,delta,base+T7O_MAC_FX_CHECKPOINT_RESTORE_RETURN);
        if (mode) unchanged();else assert(read_pointer(fx+0x2d78)==pool);
    }
    fixture();snapshot();restore_for(fx,delta,base+T7O_MAC_FX_CHECKPOINT_RESTORE_RETURN+1);unchanged();
    t7o_mac_checkpoint_deactivate();restore_for(fx,delta,base+T7O_MAC_FX_CHECKPOINT_RESTORE_RETURN);unchanged();assert(originals==5);
    puts("AAE checkpoint FX: unordered free-list relocation, malformed-state preservation, empty list, caller/mode/unload guards passed (substituted engine)");
}
