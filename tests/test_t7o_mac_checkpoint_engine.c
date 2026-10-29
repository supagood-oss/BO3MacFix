#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#define T7O_DEV_NATIVE 1
static void *test_address(uint32_t);
static bool test_readable(const char *);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_MAC_READABLE_STRING(value) test_readable(value)
#include "../source/t7o_mac_checkpoint.c"
bool t7o_mac_checkpoint_disk_install(uint64_t b) { (void)b;return true; }
bool t7o_mac_checkpoint_disk_activate(void) { return true; }
void t7o_mac_checkpoint_disk_deactivate(void) { }
void t7o_mac_checkpoint_disk_tick(void) { }
void t7o_mac_checkpoint_disk_frame_begin(int c) { (void)c; }
static uint32_t session,sizes[2],indices[4],states[4],server_count=2;
static uintptr_t buffers[4],connection_pointer,server_pointer;
static uint8_t files[0x1000e0],extra[0x2448],flags[6],ui[2*0x1078];
static uint8_t connections[2*0x25780],servers[2*T7O_MAC_SV_CLIENT_STRIDE];
static CheckpointBuiltin row;static const char *map="zm_test";
static unsigned frames,original_saves,creates,loads;
static const char *last_string;
static void savegame_mock(unsigned i) { assert(i==0);++original_saves; }
static void frame_mock(int c) { assert(c==0 || c==1);++frames; }
static void create_mock(void) { ++creates; }
static void load_mock(void *file) { assert(file==files);assert(atomic_load(&restoring));++loads; }
static void message_mock(void *msg,const char *value) { assert(msg==files);last_string=value; }
static void *dvar_mock(const char *name) { assert(!strcmp(name,"mapname"));return &map; }
static const char *text_mock(const void *p) { return *(const char *const *)p; }
static bool test_readable(const char *p) { return p && p!=(const char *)1; }
static void *test_address(uint32_t rva) {
    switch (rva) {
    case T7O_MAC_SESSION_FLAGS:return &session;
    case T7O_MAC_CHECKPOINT_SIZES:return sizes;
    case T7O_MAC_CHECKPOINT_BUFFERS:return buffers;
    case T7O_MAC_CHECKPOINT_INDICES:return indices;
    case T7O_MAC_CHECKPOINT_FILES:return files;
    case T7O_MAC_CHECKPOINT_EXTRA:return extra;
    case T7O_MAC_CHECKPOINT_STATES:return states;
    case T7O_MAC_CHECKPOINT_FLAGS:return flags;
    case T7O_MAC_SAVEGAME_CREATE_ROW:return &row;
    case T7O_MAC_SAVEGAME_CREATE:return (void *)savegame_mock;
    case T7O_MAC_CLIENT_CONNECTIONS:return &connection_pointer;
    case T7O_MAC_CLIENT_UI:return ui;
    case T7O_MAC_SV_CLIENTS:return &server_pointer;
    case T7O_MAC_SV_CLIENT_COUNT:return &server_count;
    default:abort();
    }
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *f,void **o) { (void)p;(void)f;(void)o;abort(); }
static bool zeroes(const void *p,size_t n) { const uint8_t *v=p;for (size_t i=0;i<n;++i) if (v[i]) return false;return true; }
int main(void) {
    installed=true;base=0x100000000;dvar_find=dvar_mock;dvar_text=text_mock;create_checkpoint=create_mock;
    original_frame=frame_mock;original_server_load=load_mock;original_message_string=message_mock;
    row=(CheckpointBuiltin){.hash=t7o_script_hash("savegame_create"),.max_args=1,.function=savegame_mock};
    ++row.max_args;assert(!t7o_mac_checkpoint_activate());--row.max_args;
    assert(t7o_mac_checkpoint_activate() && row.function==savegame_create);
    memset(files,0x55,sizeof(files));memset(extra,0x55,sizeof(extra));memset(flags,0x55,sizeof(flags));
    checkpoint_frame(1);assert(frames==1 && !buffers[0]);
    map="core_frontend";checkpoint_frame(0);assert(frames==2 && !buffers[0]);
    map="zm_test";session=1;checkpoint_frame(0);assert(frames==3 && !buffers[0]);session=0;
    checkpoint_frame(0);assert(frames==4 && sizes[0]==0x480000 && sizes[1]==0x280000);
    for (unsigned i=0;i<4;++i) assert(buffers[i]==(uintptr_t)owned_buffers[i] && zeroes(owned_buffers[i],i<2 ? 0x480000:0x280000));
    assert(zeroes(files,sizeof(files)) && zeroes(extra,sizeof(extra)) && zeroes(flags,sizeof(flags)));
    files[0]=42;flags[5]=1;assert(prepare_buffers() && files[0]==42 && flags[5]==1);
    savegame_create(0);assert(!creates);flags[5]=0;savegame_create(0);assert(creates==1);
    map="core_frontend";savegame_create(0);assert(creates==1 && !original_saves);
    session=1;savegame_create(0);assert(original_saves==1);session=0;map="zm_test";
    memset(servers,0x33,sizeof(servers));memset(connections,0x44,sizeof(connections));
    connection_pointer=(uintptr_t)connections;server_pointer=(uintptr_t)servers;
    for (unsigned c=0;c<2;++c) {
        put32(ui+c*0x1078+8,c==0 ? 11:0);
        put32(connections+c*0x25780+0x130,0xfffffffd);put32(connections+c*0x25780+0x134,1);
        put32(servers+c*T7O_MAC_SV_CLIENT_STRIDE,c==0 ? 5:0);
        put32(servers+c*T7O_MAC_SV_CLIENT_STRIDE+0x5030,0xffffffff);
    }
    t7o_mac_checkpoint_client_load_begin(files+0x80070);assert(atomic_load(&restoring));
    const char *valid="valid command";
    message_string_for(files,valid,base+T7O_MAC_RELIABLE_STRING_RETURN);assert(last_string==valid);
    message_string_for(files,(const char *)1,base+T7O_MAC_RELIABLE_STRING_RETURN);assert(last_string && !*last_string);
    message_string_for(files,(const char *)1,base+T7O_MAC_RELIABLE_STRING_RETURN+1);assert(last_string==(const char *)1);
    t7o_mac_checkpoint_client_load_end(files+0x80070);assert(atomic_load(&restoring) && atomic_load(&restored_parts)==2);
    assert(zeroes(connections+0x138,0x400) && read32(connections+0x134)==0xfffffffd);
    assert(!zeroes(connections+0x25780+0x138,0x400) && read32(connections+0x25780+0x134)==1);
    load_server(files);assert(loads==1 && !atomic_load(&restoring) && atomic_load(&restored_parts)==3);
    assert(zeroes(servers+0x4830,0x800) && read32(servers+0x5034)==0xffffffff && read32(servers+0x5038)==0xffffffff);
    assert(!zeroes(servers+T7O_MAC_SV_CLIENT_STRIDE+0x4830,0x800));
    assert(!begin_load(files+1,0) && !begin_load(files+0x80070,0));
    assert(begin_load(files,0));end_load(0);assert(!atomic_load(&restoring));
    /* New loads clear their own completion bit, in either ordering. */
    assert(begin_load(files+0xc00a8,1) && atomic_load(&restoring));assert(begin_load(files+0x40038,0));
    end_load(0);assert(atomic_load(&restoring));end_load(1);assert(!atomic_load(&restoring));
    t7o_mac_checkpoint_deactivate();assert(row.function==savegame_mock && !atomic_load(&restoring));
    savegame_create(0);assert(original_saves==2);assert(buffers[0]==(uintptr_t)owned_buffers[0]);
    puts("Mac checkpoint buffers, mode/busy/slot guards, script registration, reliable queues, wraparound, two-part restore and unload tests passed (substituted engine)");
}
