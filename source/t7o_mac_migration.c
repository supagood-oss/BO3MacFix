#include "t7o_mac_migration.h"
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
static void (*original_check)(void),(*original_rating)(void);
static bool (*server_running)(void),(*migration_running)(void);
static void (*start_migration)(bool);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t read32(const void *p) { uint32_t value;memcpy(&value,p,4);return value; }
static void write32(void *p,uint32_t value) { memcpy(p,&value,4); }
static bool default_game(void) { return (read32(address(T7O_MAC_SESSION_FLAGS))&0x3c0)==0x80; }
static void migration_check(void) {
    if (!atomic_load(&active) || !default_game()) { original_check();return; }
    /* AAE broadens the two predicate calls in the migration command. The Mac
     * wrapper has an equivalent start path with different predicate helpers.
     * Preserve the engine's running-server and already-migrating checks. */
    if (server_running() && !migration_running()) start_migration(true);
}
static void select_host_for(uintptr_t caller) {
    uint32_t changed=0;
    uint8_t *table=address(T7O_MAC_MIGRATION_CLIENTS);
    uint32_t flags=read32(address(T7O_MAC_SESSION_FLAGS));
    if (atomic_load(&active) && caller==base+T7O_MAC_SELECT_HOST_RETURN &&
        (flags&0x3c0)==0x80 && (flags&0x3c000)==0x4000 &&
        read32(address(T7O_MAC_MIGRATION_STATE))==1 &&
        read32(address(T7O_MAC_MIGRATION_BEST))==UINT32_MAX &&
        *(const uint8_t *)address(T7O_MAC_MIGRATION_RUNNING)) {
        unsigned count=0;bool eligible=true;
        for (unsigned i=0;i<18;++i) {
            const uint8_t *entry=table+i*32;uint32_t state=read32(entry);
            if (!state) continue;
            if (state!=2 || read32(entry+0x18)!=0 || read32(entry+0x10)==0) { eligible=false;break; }
            ++count;
        }
        if (eligible && count>=2 && count<=4) {
            for (unsigned i=0;i<18;++i) if (read32(table+i*32)==2) {
                write32(table+i*32+0x18,2);changed|=UINT32_C(1)<<i;
            }
        }
    }
    original_rating();
    /* Restore just our temporary zero-to-two ratings. An engine update made
     * inside host selection takes precedence, as in the Windows component. */
    for (unsigned i=0;i<18;++i)
        if ((changed&(UINT32_C(1)<<i)) && read32(table+i*32+0x18)==2) write32(table+i*32+0x18,0);
}
static void select_host(void) { select_host_for((uintptr_t)__builtin_return_address(0)); }
void t7o_mac_migration_deactivate(void) { atomic_store(&active,false); }
bool t7o_mac_migration_activate(void) {
    if (!installed) return false;
    atomic_store(&active,true);return true;
}
bool t7o_mac_migration_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t check[]={0x55,0x48,0x89,0xe5,0xe8,0xd1,0xd5,0xe6,0xff};
    const uint8_t rating[]={0x55,0x48,0x89,0xe5,0x41,0x57};
    if (memcmp(prepatch_original_code(address(T7O_MAC_MIGRATION_CHECK),sizeof(check)),check,sizeof(check)) ||
        memcmp(prepatch_original_code(address(T7O_MAC_SELECT_HOST),sizeof(rating)),rating,sizeof(rating))) return false;
    server_running=address(T7O_MAC_SERVER_RUNNING);migration_running=address(T7O_MAC_IS_MIGRATING);
    start_migration=address(T7O_MAC_START_MIGRATION);
    if (DobbyHook(address(T7O_MAC_MIGRATION_CHECK),(void *)migration_check,(void **)&original_check)!=0 ||
        DobbyHook(address(T7O_MAC_SELECT_HOST),(void *)select_host,(void **)&original_rating)!=0) return false;
    installed=true;return true;
}
