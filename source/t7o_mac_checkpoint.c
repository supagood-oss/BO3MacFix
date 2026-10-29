#include "t7o_mac_checkpoint.h"
#include "t7o_mac_offsets.h"
#include "prepatch.h"
#include "dobby.h"
#include "t7o_core.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static void (*original_restore)(void *,uintptr_t);
static void (*original_frame)(int),(*original_server_load)(void *);
static void (*original_message_string)(void *,const char *);
static void *(*dvar_find)(const char *);
static const char *(*dvar_text)(const void *);
static void (*original_savegame)(unsigned),(*create_checkpoint)(void);
typedef struct { uint32_t hash,min_args,max_args,pad;void (*function)(unsigned);uint32_t kind,pad2; } CheckpointBuiltin;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void *owned_buffers[4];
static atomic_uint restored_parts;
static atomic_bool restoring;
static bool savegame_registered;
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uintptr_t read_pointer(const void *p) { uintptr_t v;memcpy(&v,p,8);return v; }
static void write_pointer(void *p,uintptr_t v) { memcpy(p,&v,8); }
static void put32(void *p,uint32_t value) { memcpy(p,&value,4); }
static bool zombies_map(void) {
    if (!atomic_load(&active) || (read32(address(T7O_MAC_SESSION_FLAGS))&15)!=0 || !dvar_find || !dvar_text) return false;
    void *dvar=dvar_find("mapname");const char *name=dvar ? dvar_text(dvar) : NULL;
    return name && *name && strcmp(name,"core_frontend");
}
/* The ordinary Mac Zombies initializer allocates no client checkpoint buffers.
 * Match the native extension's server/client sizes, before MemFile_Init runs.
 * These process-lifetime buffers are retained across mod/map switches because
 * an engine memory-file can still contain their addresses. */
static bool prepare_buffers(void) {
    uint8_t *sizes=address(T7O_MAC_CHECKPOINT_SIZES),*buffers=address(T7O_MAC_CHECKPOINT_BUFFERS);
    bool sufficient=read32(sizes)>=0x480000 && read32(sizes+4)>=0x280000;
    for (unsigned i=0;i<4;++i) sufficient&=read_pointer(buffers+i*8)!=0;
    if (sufficient) return true;
    for (unsigned i=0;i<4;++i) {
        if (!owned_buffers[i]) owned_buffers[i]=calloc(1,i<2 ? 0x480000 : 0x280000);
        if (!owned_buffers[i]) return false;
    }
    memset(address(T7O_MAC_CHECKPOINT_FILES),0,0x1000e0);
    memset(address(T7O_MAC_CHECKPOINT_INDICES),0,16);
    memset(address(T7O_MAC_CHECKPOINT_EXTRA),0,0x2448);
    memset(address(T7O_MAC_CHECKPOINT_STATES),0,16);
    memset(address(T7O_MAC_CHECKPOINT_FLAGS),0,6);
    for (unsigned i=0;i<4;++i) write_pointer(buffers+i*8,(uintptr_t)owned_buffers[i]);
    put32(sizes,0x480000);put32(sizes+4,0x280000);
    atomic_store(&restored_parts,0);atomic_store(&restoring,false);return true;
}
static void checkpoint_frame(int channel) {
    if (channel==0 && zombies_map()) { pthread_mutex_lock(&lock);prepare_buffers();pthread_mutex_unlock(&lock); }
    original_frame(channel);
}
static bool pending_checkpoint(void) {
    const uint8_t *flags=address(T7O_MAC_CHECKPOINT_FLAGS);
    for (unsigned i=0;i<6;++i) if (flags[i]) return true;
    return false;
}
static void savegame_create(unsigned instance) {
    if (!atomic_load(&active)) { original_savegame(instance);return; }
    if ((read32(address(T7O_MAC_SESSION_FLAGS))&15)!=0) { original_savegame(instance);return; }
    if (!zombies_map()) return;
    pthread_mutex_lock(&lock);bool ready=prepare_buffers() && !pending_checkpoint();pthread_mutex_unlock(&lock);
    if (ready) create_checkpoint();
}
static bool checkpoint_file(void *file,unsigned channel) {
    uintptr_t first=(uintptr_t)address(T7O_MAC_CHECKPOINT_FILES)+(size_t)channel*0x80070;
    return (uintptr_t)file==first || (uintptr_t)file==first+0x40038;
}
static bool begin_load(void *file,unsigned channel) {
    if (!zombies_map() || !checkpoint_file(file,channel)) return false;
    atomic_fetch_and(&restored_parts,~(1u<<channel));atomic_store(&restoring,true);return true;
}
static void end_load(unsigned channel) {
    unsigned before=atomic_fetch_or(&restored_parts,1u<<channel);
    if (before==(3u^(1u<<channel))) atomic_store(&restoring,false);
}
static void clear_client_commands(void) {
    uint8_t *connections=(void *)read_pointer(address(T7O_MAC_CLIENT_CONNECTIONS));
    if (!connections) return;
    for (unsigned c=0;c<2;++c) {
        const uint8_t *ui=(const uint8_t *)address(T7O_MAC_CLIENT_UI)+(size_t)c*0x1078;
        if (!read32(ui+8)) continue;
        uint8_t *connection=connections+(size_t)c*0x25780;
        uint32_t sequence=read32(connection+0x130);
        memset(connection+0x138,0,0x400);put32(connection+0x134,sequence);
    }
}
static void clear_server_commands(void) {
    uint8_t *clients=(void *)read_pointer(address(T7O_MAC_SV_CLIENTS));unsigned count=read32(address(T7O_MAC_SV_CLIENT_COUNT));
    if (!clients || count<1 || count>18) return;
    for (unsigned c=0;c<count;++c) {
        uint8_t *client=clients+(size_t)c*T7O_MAC_SV_CLIENT_STRIDE;
        if (!read32(client)) continue;
        uint32_t sequence=read32(client+0x5030);memset(client+0x4830,0,0x800);
        put32(client+0x5034,sequence);put32(client+0x5038,sequence);
    }
}
void t7o_mac_checkpoint_client_load_begin(void *file) { begin_load(file,1); }
void t7o_mac_checkpoint_client_load_end(void *file) {
    if (!zombies_map() || !checkpoint_file(file,1)) return;
    clear_client_commands();end_load(1);
}
static void load_server(void *file) {
    bool repair=begin_load(file,0);original_server_load(file);
    if (repair) { clear_server_commands();end_load(0); }
}
#ifndef T7O_MAC_READABLE_STRING
static bool readable_string(const char *value) {
#if defined(__APPLE__)
    uintptr_t at=(uintptr_t)value;char buffer[256];size_t remaining=1024;
    while (at && remaining) {
        size_t n=sizeof(buffer),page_remaining=0x1000-(at&0xfff);
        if (n>remaining) n=remaining;
        if (n>page_remaining) n=page_remaining;
        mach_vm_size_t done=0;
        if (mach_vm_read_overwrite(mach_task_self(),at,n,(mach_vm_address_t)buffer,&done)!=KERN_SUCCESS || done!=n) return false;
        if (memchr(buffer,0,n)) return true;
        if (at>UINTPTR_MAX-n) return false;
        at+=n;remaining-=n;
    }
#else
    (void)value;
#endif
    return false;
}
#define T7O_MAC_READABLE_STRING(value) readable_string(value)
#endif
static void message_string_for(void *message,const char *value,uintptr_t caller) {
    if (atomic_load(&active) && atomic_load(&restoring) && caller==base+T7O_MAC_RELIABLE_STRING_RETURN && !T7O_MAC_READABLE_STRING(value)) value="";
    original_message_string(message,value);
}
static void message_string(void *message,const char *value) { message_string_for(message,value,(uintptr_t)__builtin_return_address(0)); }
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
void t7o_mac_checkpoint_deactivate(void) {
    atomic_store(&active,false);atomic_store(&restoring,false);atomic_store(&restored_parts,0);
    if (savegame_registered) {
        CheckpointBuiltin *row=address(T7O_MAC_SAVEGAME_CREATE_ROW);
        if (row->function==savegame_create) row->function=original_savegame;
        savegame_registered=false;
    }
}
bool t7o_mac_checkpoint_activate(void) {
    if (!installed) return false;
    if (atomic_load(&active)) return true;
    CheckpointBuiltin *row=address(T7O_MAC_SAVEGAME_CREATE_ROW);
    if (row->hash!=t7o_script_hash("savegame_create") || row->min_args!=0 || row->max_args!=1 ||
        row->function!=(void (*)(unsigned))address(T7O_MAC_SAVEGAME_CREATE)) return false;
    original_savegame=row->function;row->function=savegame_create;savegame_registered=true;
    atomic_store(&active,true);return true;
}
bool t7o_mac_checkpoint_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint32_t functions[]={T7O_MAC_CHECKPOINT_FRAME,T7O_MAC_CHECKPOINT_SERVER_LOAD,T7O_MAC_MESSAGE_STRING,T7O_MAC_CREATE_CHECKPOINT};
    const uint8_t prologue[]={0x55,0x48,0x89,0xe5};
    for (unsigned i=0;i<sizeof(functions)/sizeof(functions[0]);++i)
        if (memcmp(prepatch_original_code(address(functions[i]),4),prologue,4)) return false;
    const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x48,0x85,0xf6};
    if (memcmp(prepatch_original_code(address(T7O_MAC_FX_CHECKPOINT_RESTORE),sizeof(prefix)),prefix,sizeof(prefix))) return false;
    if (DobbyHook(address(T7O_MAC_FX_CHECKPOINT_RESTORE),(void *)restore,(void **)&original_restore)!=0) return false;
    dvar_find=(void *)address(T7O_MAC_DVAR_FIND_NAME);dvar_text=(void *)address(T7O_MAC_DVAR_VALUE_STRING);
    create_checkpoint=(void *)address(T7O_MAC_CREATE_CHECKPOINT);
    if (DobbyHook(address(T7O_MAC_CHECKPOINT_FRAME),(void *)checkpoint_frame,(void **)&original_frame) ||
        DobbyHook(address(T7O_MAC_CHECKPOINT_SERVER_LOAD),(void *)load_server,(void **)&original_server_load) ||
        DobbyHook(address(T7O_MAC_MESSAGE_STRING),(void *)message_string,(void **)&original_message_string)) return false;
    installed=true;return true;
}
