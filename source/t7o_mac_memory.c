#include "t7o_mac_memory.h"
#include "t7o_mac_offsets.h"
#include "prepatch.h"
#include "dobby.h"
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static void (*original_register)(int,unsigned,int,const char *,uint64_t,unsigned);
static void register_pool(int stack,unsigned pool,int type,const char *name,uint64_t size,unsigned flags) {
    /* Mac PMem_Init registers main as (0,0,2,"main",2 GiB,0x1e).
     * Type 2 reserves address space; type 1 immediately commits physical pages.
     * GPU, physical, other stack/pool and already larger reservations keep their
     * original sizes. The engine's own 64-bit allocator/commit paths are retained. */
    if (stack==0 && pool==0 && type==2 && name && !strcmp(name,"main") &&
        size==UINT64_C(0x80000000) && flags==0x1e) size=UINT64_C(0x100000000);
    original_register(stack,pool,type,name,size,flags);
}
bool t7o_mac_memory_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    void *entry=(void *)(uintptr_t)(game_base+T7O_MAC_POOL_REGISTER);
    if (memcmp(prepatch_original_code(entry,sizeof(prefix)),prefix,sizeof(prefix))) return false;
    if (DobbyHook(entry,(void *)register_pool,(void **)&original_register)!=0) return false;
    base=game_base;installed=true;return true;
}
