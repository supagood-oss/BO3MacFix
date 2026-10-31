#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static void *test_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_migration.c"
static uint32_t session=0x4080,state=1,best=UINT32_MAX;
static uint8_t running=1,table[18*32],observed[sizeof(table)];
static bool have_server=true,migrating;
static unsigned check_calls,starts,rating_calls;
static bool change_rating;
static uint8_t private_lobby[0x458];static unsigned notifications;static int last_lobby;
static bool initial_success,retry_success=true;
static void *test_address(uint32_t rva) {
    switch (rva) {
    case T7O_MAC_SESSION_FLAGS:return &session;
    case T7O_MAC_MIGRATION_STATE:return &state;
    case T7O_MAC_MIGRATION_BEST:return &best;
    case T7O_MAC_MIGRATION_RUNNING:return &running;
    case T7O_MAC_MIGRATION_CLIENTS:return table;
    case T7O_MAC_PRIVATE_HOST_SESSION:return private_lobby;
    default:abort();
    }
}
static bool server_mock(void) { return have_server; }
static bool migrating_mock(void) { return migrating; }
static void start_mock(bool value) { assert(value);++starts; }
static void check_mock(void) { ++check_calls; }
static bool notify_mock(int controller,uint64_t host,int lobby,const void *clients,int count) {
    assert(controller==2 && host==123456 && clients==observed && count==2);++notifications;last_lobby=lobby;
    return lobby==1 ? initial_success:retry_success;
}
static void rating_mock(void) {
    ++rating_calls;memcpy(observed,table,sizeof(table));
    if (change_rating) write32(table+0x18,7);
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
static void reset(unsigned count) {
    memset(table,0,sizeof(table));
    for (unsigned i=0;i<count;++i) { write32(table+i*32,2);write32(table+i*32+0x10,1); }
}
int main(void) {
    original_check=check_mock;original_rating=rating_mock;server_running=server_mock;
    migration_running=migrating_mock;start_migration=start_mock;base=0x100000000;
    original_notify=notify_mock;
    assert(!t7o_mac_migration_install(base,false) && !t7o_mac_migration_activate());
    migration_check();assert(check_calls==1 && !starts);
    installed=true;assert(t7o_mac_migration_activate());migration_check();assert(starts==1);
    have_server=false;migration_check();assert(starts==1);have_server=true;
    migrating=true;migration_check();assert(starts==1);migrating=false;
    session=0;migration_check();assert(check_calls==2);session=0x4080;
    for (unsigned count=0;count<=18;++count) {
        reset(count);select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);
        for (unsigned i=0;i<18;++i) {
            assert(read32(observed+i*32+0x18)==(i<count && count>=2 && count<=4 ? 2u : 0u));
            assert(read32(table+i*32+0x18)==0);
        }
    }
    reset(2);select_host_for(base+1);assert(!read32(observed+0x18));
    reset(2);state=4;select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);assert(!read32(observed+0x18));state=1;
    reset(2);best=0;select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);assert(!read32(observed+0x18));best=UINT32_MAX;
    reset(2);running=0;select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);assert(!read32(observed+0x18));running=1;
    reset(2);write32(table+32,1);select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);assert(!read32(observed+0x18));
    reset(2);write32(table+32+0x10,0);select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);assert(!read32(observed+0x18));
    reset(2);write32(table+32+0x18,3);select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);
    assert(!read32(observed+0x18) && read32(table+32+0x18)==3);
    reset(2);change_rating=true;select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);
    assert(read32(table+0x18)==7 && !read32(table+32+0x18));change_rating=false;
    state=4;write32(private_lobby+0x40,2);uint64_t host=123456;memcpy(private_lobby+0xf8+17*0x30,&host,8);
    assert(notify_for(base+T7O_MAC_MIGRATE_CLIENTS_RETURN,2,host,1,observed,2) && notifications==2 && last_lobby==0);
    initial_success=true;assert(notify_for(base+T7O_MAC_MIGRATE_CLIENTS_RETURN,2,host,1,observed,2) && notifications==3);initial_success=false;
    retry_success=false;assert(!notify_for(base+T7O_MAC_MIGRATE_CLIENTS_RETURN,2,host,1,observed,2) && notifications==5);retry_success=true;
    assert(!notify_for(base,2,host,1,observed,2) && notifications==6);
    write32(private_lobby+0x40,1);assert(!notify_for(base+T7O_MAC_MIGRATE_CLIENTS_RETURN,2,host,1,observed,2) && notifications==7);write32(private_lobby+0x40,2);
    memset(private_lobby+0xf8+17*0x30,0,8);assert(!notify_for(base+T7O_MAC_MIGRATE_CLIENTS_RETURN,2,host,1,observed,2) && notifications==8);
    memcpy(private_lobby+0xf8,&host,8);session=0x80;assert(!notify_for(base+T7O_MAC_MIGRATE_CLIENTS_RETURN,2,host,1,observed,2) && notifications==9);session=0x4080;
    state=1;assert(!notify_for(base+T7O_MAC_MIGRATE_CLIENTS_RETURN,2,host,1,observed,2) && notifications==10);
    t7o_mac_migration_deactivate();reset(2);select_host_for(base+T7O_MAC_SELECT_HOST_RETURN);
    assert(!read32(observed+0x18));migration_check();assert(check_calls==3 && rating_calls>0);
    puts("AAE migration: eligibility, scoped ratings, private-lobby retry/membership, sender arguments and inactive fallback passed (substituted engine)");
}
