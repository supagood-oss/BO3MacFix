#include "t7o_mac_fx.h"
#include "t7o_mac_offsets.h"
#include "t7o_core.h"
#include "prepatch.h"
#include "dobby.h"
#include "utilities.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
static uint64_t base;
static bool installed;
static atomic_bool active;
static T7oFxStorage storage;
typedef int (*RegisterDef)(unsigned,unsigned,void *,int,const char *,void *,bool);
typedef int (*FindIndex)(unsigned,unsigned,void *,int,const char *,bool);
typedef void *(*GetPointer)(unsigned,int);
typedef const char *(*GetName)(unsigned,int);
typedef int (*GetIndex)(unsigned,const char *);
typedef void (*Unregister)(unsigned,const char *);
typedef void (*Reset)(unsigned);
static RegisterDef original_register;
static FindIndex cache_find;
static GetPointer original_pointer;
static GetName original_name;
static GetIndex original_index;
static Unregister original_unregister;
static Reset original_reset;

bool t7o_mac_fx_active(void) { return atomic_load_explicit(&active,memory_order_acquire); }
static void *old_table(unsigned instance) {
    return (void *)(uintptr_t)(base+T7O_MAC_CLIENT_FX_TABLE+(uint64_t)instance*T7O_MAC_CACHE_INSTANCE_STRIDE);
}
static int index_in_extended(unsigned instance,const char *name,bool allocate) {
    if (instance>1 || !name) return 0;
    return cache_find(instance,27,storage.entries[instance],(int)storage.capacity,name,allocate);
}
static int register_def(unsigned instance,unsigned type,void *table,int capacity,
    const char *name,void *definition,bool increase) {
    /* Both Mac client-FX registration wrappers inline their generic call.
     * Intercept the generic routine so neither wrapper retains the old table. */
    if (t7o_mac_fx_active() && type==27 && instance<2 && table==old_table(instance) && capacity==1024) {
        table=storage.entries[instance];capacity=(int)storage.capacity;
    }
    return original_register(instance,type,table,capacity,name,definition,increase);
}
static void *pointer(unsigned instance,int index) {
    if (!t7o_mac_fx_active()) return original_pointer(instance,index);
    void *slot=t7o_fx_slot(&storage,(int)instance,index), *definition=NULL;
    if (slot) memcpy(&definition,slot,sizeof(definition));
    return definition;
}
static const char *name(unsigned instance,int index) {
    if (!t7o_mac_fx_active()) return original_name(instance,index);
    void *definition=pointer(instance,index);const char *text=NULL;
    if (definition) memcpy(&text,definition,sizeof(text));
    return text ? text : "";
}
static int index_of(unsigned instance,const char *text) {
    if (!t7o_mac_fx_active()) return original_index(instance,text);
    return index_in_extended(instance,text,false);
}
static void unregister_fx(unsigned instance,const char *text) {
    if (!t7o_mac_fx_active()) { original_unregister(instance,text);return; }
    int index=index_in_extended(instance,text,false);
    uint8_t *slot=t7o_fx_slot(&storage,(int)instance,index);
    if (!slot || index==0 || slot[12]==0) return;
    --slot[12];if (!slot[12]) memset(slot,0,16);
}
static void reset(unsigned instance) {
    if (t7o_mac_fx_active()) t7o_fx_clear(&storage,(int)instance);
    original_reset(instance);
}
bool t7o_mac_fx_activate(void) {
    if (!installed) return false;
    if (t7o_mac_fx_active()) return true;
    const void *old[2]={old_table(0),old_table(1)};
    /* Must run at a quiescent mod-load boundary, before gameplay/CSC workers
     * use the cache. This lifecycle boundary still needs Mac qualification. */
    if (!t7o_fx_prepare(&storage,old,1024,2048)) return false;
    storage.bound=true;
    atomic_store_explicit(&active,true,memory_order_release);
    return true;
}
bool t7o_mac_fx_install(uint64_t game_base,const char *image_path) {
    if (!T7O_DEV_NATIVE || installed || !image_path) return installed;
    char original[4096];size_t length=strlen(image_path);
    if (length>=sizeof(original)) return false;
    memcpy(original,image_path,length+1);
    char *file=strrchr(original,'/');
    if (!file || (size_t)(file-original)+1+strlen("CoDBlkOps3_Exe")>=sizeof(original)) return false;
    strcpy(file+1,"CoDBlkOps3_Exe");
    const uint8_t expected[20]={0x5e,0x42,0x37,0x1b,0x6a,0x16,0xf2,0xb1,0x1e,0x63,
        0x7f,0x48,0xe3,0x85,0x54,0x62,0x55,0x70,0xaa,0x9f};
    uint8_t digest[20];
    if (!get_file_sha1(original,digest) || memcmp(digest,expected,sizeof(expected))) return false;
    base=game_base;
    cache_find=(FindIndex)(uintptr_t)(base+T7O_MAC_CACHE_FIND_INDEX);
    struct Hook { uint32_t offset;uint8_t length,bytes[7];void *replacement;void **original; } hooks[]={
        {T7O_MAC_FX_REGISTER_DEF,6,{0x55,0x48,0x89,0xe5,0x41,0x57},(void *)register_def,(void **)&original_register},
        {T7O_MAC_FX_GET_POINTER,6,{0x55,0x48,0x89,0xe5,0x89,0xf8},(void *)pointer,(void **)&original_pointer},
        {T7O_MAC_FX_GET_NAME,6,{0x55,0x48,0x89,0xe5,0x89,0xf8},(void *)name,(void **)&original_name},
        {T7O_MAC_FX_GET_INDEX,7,{0x55,0x48,0x89,0xe5,0x48,0x89,0xf0},(void *)index_of,(void **)&original_index},
        {T7O_MAC_FX_UNREGISTER,7,{0x55,0x48,0x89,0xe5,0x48,0x89,0xf0},(void *)unregister_fx,(void **)&original_unregister},
        {T7O_MAC_CACHE_RESET,5,{0x55,0x48,0x89,0xe5,0x53},(void *)reset,(void **)&original_reset}
    };
    /* Validate all entry bytes before installing the first hook. A partial
     * hook failure leaves active=false, so installed wrappers call originals. */
    for (size_t i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i)
        if (memcmp(prepatch_original_code((void *)(uintptr_t)(base+hooks[i].offset),hooks[i].length),
            hooks[i].bytes,hooks[i].length)) return false;
    for (size_t i=0;i<sizeof(hooks)/sizeof(hooks[0]);++i)
        if (DobbyHook((void *)(uintptr_t)(base+hooks[i].offset),hooks[i].replacement,hooks[i].original)!=0) {
            printf("T7Overcharged: Mac FX hook installation failed; extension remains inactive\n");return false;
        }
    installed=true;return true;
}
