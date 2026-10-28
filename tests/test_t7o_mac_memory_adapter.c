#include <assert.h>
#include <stdio.h>
#include <string.h>
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_memory.c"
static uint64_t observed;
static int hook_calls,register_calls,expected_stack,expected_type;
static unsigned expected_pool,expected_flags;
static const char *expected_name;
static const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57};
static void register_mock(int stack,unsigned pool,int type,const char *name,uint64_t size,unsigned flags) {
    assert(stack==expected_stack && pool==expected_pool && type==expected_type);
    assert(name==expected_name && flags==expected_flags);observed=size;++register_calls;
}
const void *prepatch_original_code(const void *entry,size_t n) {
    assert((uintptr_t)entry==UINT64_C(0x100000000)+T7O_MAC_POOL_REGISTER && n==sizeof(prefix));return prefix;
}
int BO3MacFix_Hook(void *entry,void *replacement,void **original) {
    assert((uintptr_t)entry==UINT64_C(0x100000000)+T7O_MAC_POOL_REGISTER);
    assert(replacement==(void *)register_pool);*original=(void *)register_mock;++hook_calls;return 0;
}
static void check(int stack,unsigned pool,int type,const char *name,uint64_t size,unsigned flags,uint64_t wanted) {
    expected_stack=stack;expected_pool=pool;expected_type=type;expected_name=name;expected_flags=flags;
    int before=register_calls;register_pool(stack,pool,type,name,size,flags);
    assert(register_calls==before+1 && observed==wanted);
}
int main(void) {
    assert(!t7o_mac_memory_install(UINT64_C(0x100000000),false) && hook_calls==0);
    assert(t7o_mac_memory_install(UINT64_C(0x100000000),true) && hook_calls==1);
    assert(t7o_mac_memory_install(UINT64_C(0x100000000),true) && hook_calls==1);
    assert(!t7o_mac_memory_install(UINT64_C(0x200000000),true));
    check(0,0,2,"main",UINT64_C(0x80000000),0x1e,UINT64_C(0x100000000));
    check(0,0,1,"main",UINT64_C(0x80000000),0x1e,UINT64_C(0x80000000));
    check(0,1,2,"main",UINT64_C(0x80000000),0x1e,UINT64_C(0x80000000));
    check(1,0,2,"main",UINT64_C(0x80000000),0x1e,UINT64_C(0x80000000));
    check(0,0,2,"gpu",UINT64_C(0x80000000),0x1e,UINT64_C(0x80000000));
    check(0,0,2,NULL,UINT64_C(0x80000000),0x1e,UINT64_C(0x80000000));
    check(0,0,2,"main",UINT64_C(0x80000000),0x3e,UINT64_C(0x80000000));
    check(0,0,2,"main",UINT64_C(0x100000000),0x1e,UINT64_C(0x100000000));
    puts("Boot-time main arena reservation, other-pool preservation and exact-build gate passed");
}
