/* Exercises adapter contracts with substituted engine calls. The actual Mac
 * index routine is covered separately by verify_mac_contracts.py. No game
 * memory, Dobby patching, macOS ABI or Rosetta behavior is tested here. */
#include <assert.h>
#include <stdlib.h>
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_fx.c"

static int fallback,reset_calls,register_calls;
static unsigned observed_type;static int observed_capacity;static void *observed_table;
static bool observed_increase;
static int register_mock(unsigned instance,unsigned type,void *table,int capacity,
    const char *text,void *definition,bool increase) {
    (void)instance;(void)text;(void)definition;++register_calls;
    observed_type=type;observed_table=table;observed_capacity=capacity;observed_increase=increase;
    return 17;
}
static int find_mock(unsigned instance,unsigned type,void *table,int capacity,const char *text,bool allocate) {
    (void)allocate;assert(instance<2 && type==27 && capacity==2048);
    assert(table==storage.entries[instance]);return !strcmp(text,"effect") ? 1025 : 0;
}
static void *pointer_mock(unsigned instance,int index) { (void)instance;(void)index;return &fallback; }
static const char *name_mock(unsigned instance,int index) { (void)instance;(void)index;return "original"; }
static int index_mock(unsigned instance,const char *text) { (void)instance;(void)text;return 3; }
static void unregister_mock(unsigned instance,const char *text) { (void)instance;(void)text;++fallback; }
static void reset_mock(unsigned instance) {
    ++reset_calls;
    if (t7o_mac_fx_active()) assert(*(uint64_t *)storage.entries[instance]==0);
}
/* Install-path symbols are linked but never called by this fixture. */
bool get_file_sha1(const char *file,uint8_t *digest) { (void)file;(void)digest;abort(); }
const void *prepatch_original_code(const void *address,size_t size) { (void)address;(void)size;abort(); }
int BO3MacFix_Hook(void *address,void *replacement,void **original) { (void)address;(void)replacement;(void)original;abort(); }

int main(void) {
    uint8_t *old=calloc(2,T7O_MAC_CACHE_INSTANCE_STRIDE);assert(old);
    base=(uintptr_t)old-T7O_MAC_CLIENT_FX_TABLE;
    original_register=register_mock;original_pointer=pointer_mock;original_name=name_mock;
    original_index=index_mock;original_unregister=unregister_mock;original_reset=reset_mock;cache_find=find_mock;
    assert(pointer(0,1)==&fallback && !strcmp(name(0,1),"original") && index_of(0,"effect")==3);
    unregister_fx(0,"effect");assert(fallback==1);
    assert(!t7o_mac_fx_activate());
    old[16]=0x71;old[T7O_MAC_CACHE_INSTANCE_STRIDE+16]=0x52;
    installed=true;assert(t7o_mac_fx_activate());
    assert(storage.entries[0][16]==0x71 && storage.entries[1][16]==0x52);
    assert(t7o_mac_fx_activate()); // Must not recopy stale engine storage.
    assert(register_def(0,27,old,1024,"effect",NULL,true)==17);
    assert(register_calls==1 && observed_table==storage.entries[0] && observed_capacity==2048 && observed_increase);
    assert(register_def(1,24,old+T7O_MAC_CACHE_INSTANCE_STRIDE,256,"effect",NULL,false)==17);
    assert(observed_type==24 && observed_table==old+T7O_MAC_CACHE_INSTANCE_STRIDE && observed_capacity==256 && !observed_increase);
    const char *definition_name="fixture effect";void *definition=&definition_name;
    uint8_t *slot=t7o_fx_slot(&storage,1,1025);memcpy(slot,&definition,sizeof(definition));slot[12]=2;
    assert(pointer(1,1025)==definition && !strcmp(name(1,1025),definition_name));
    assert(index_of(1,"effect")==1025 && !pointer(2,1) && !pointer(0,2048));
    unregister_fx(1,"effect");assert(slot[12]==1 && pointer(1,1025)==definition);
    unregister_fx(1,"effect");assert(slot[12]==0 && pointer(1,1025)==NULL);
    unregister_fx(1,"missing");assert(storage.entries[1][12]==0);
    storage.entries[0][0]=1;storage.entries[1][0]=2;reset(0);
    assert(reset_calls==1 && storage.entries[0][0]==0 && storage.entries[1][0]==2);
    atomic_store(&active,false);storage.bound=false;assert(t7o_fx_discard(&storage));free(old);
    puts("Mac FX adapter migration, redirection, references and reset contracts passed (substituted engine)");
    return 0;
}
