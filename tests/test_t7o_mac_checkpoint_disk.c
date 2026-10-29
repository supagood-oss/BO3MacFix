#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void *test_address(uint32_t);
static uint64_t test_now=1000;
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_CHECKPOINT_OWNER() UINT64_C(123456789)
#define T7O_CHECKPOINT_MILLISECONDS() test_now
#include "../source/t7o_mac_checkpoint_disk.c"
static uint32_t session=0x40,sizes[2]={T7O_CP_SERVER_MAX,T7O_CP_CLIENT_MAX},indices[2],states[4];
static uintptr_t buffers[4];static uint8_t files[0x1000e0],flags[6],camera[176];
static uint8_t clients[4*T7O_MAC_SV_CLIENT_STRIDE];static uintptr_t clients_pointer=(uintptr_t)clients;
static unsigned parts,restore_calls;static bool accept_restore=true,host=true;
static char last_status[128],folder[192]="fixture",map[64]="zm_test",mod[128]="usermaps/AAE";
static void *test_address(uint32_t offset) {
    switch (offset) {
    case T7O_MAC_SESSION_FLAGS:return &session;
    case T7O_MAC_CHECKPOINT_SIZES:return sizes;
    case T7O_MAC_CHECKPOINT_BUFFERS:return buffers;
    case T7O_MAC_CHECKPOINT_FILES:return files;
    case T7O_MAC_CHECKPOINT_STATES:return states;
    case T7O_MAC_CHECKPOINT_FLAGS:return flags;
    case T7O_MAC_CHECKPOINT_INDICES:return indices;
    case T7O_MAC_SV_CLIENTS:return &clients_pointer;
    default:abort();
    }
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
void t7o_mac_checkpoint_restore_prepare(void) { parts=0; }
void t7o_mac_checkpoint_restore_cancel(void) { parts=0; }
unsigned t7o_mac_checkpoint_restore_parts(void) { return parts; }
bool t7o_mac_camera_checkpoint_valid(const uint8_t bytes[176]) { return u32(bytes)==1 && !(u32(bytes+4)&~3u); }
void t7o_mac_camera_checkpoint_export(unsigned slot,uintptr_t buffer,uint8_t bytes[176]) { assert(slot<2 && buffer);memcpy(bytes,camera,176); }
bool t7o_mac_camera_checkpoint_import(unsigned slot,uintptr_t buffer,const uint8_t bytes[176]) { assert(slot<2 && buffer);memcpy(camera,bytes,176);return true; }
static void *find(const char *name) { return (void *)name; }
static const char *text(const void *p) {
    const char *name=p;
    if (!strcmp(name,"mapname")) return map;
    if (!strcmp(name,"fs_game")) return mod;
    if (!strcmp(name,"com_maxclients")) return "4";
    if (!strcmp(name,"aae_savegame_folder")) return folder;
    return "";
}
static void set(const char *name,const char *value,bool force) { assert(force);if (!strcmp(name,"bo3macfix_checkpoint_status")) snprintf(last_status,sizeof(last_status),"%s",value); }
static bool running(void) { return host; }
static bool restart(void) { ++restore_calls;if (accept_restore) flags[4]=1;return accept_restore; }
static void finish_job(void) { assert(job.running);if (!job.joined) { pthread_join(job.thread,NULL);job.joined=true; }assert(atomic_load(&job.done)); }
static void capture(unsigned value) {
    for (unsigned c=0;c<2;++c) { flags[c]=1;t7o_mac_checkpoint_disk_frame_begin((int)c);flags[c]=0; }
    for (unsigned c=0;c<2;++c) {
        unsigned slot=(indices[c]&1)+c*2;uint8_t *f=files+slot*0x40038,*b=(void *)buffers[slot];
        put32(b,12);put32(b+4,value);memcpy(f,&buffers[slot],8);put32(f+0xc,12);put32(f+0x1c,UINT32_MAX);
        states[slot]=2;states[slot^1]=0;++indices[c];
    }
}
int main(void) {
    assert(!t7o_mac_checkpoint_disk_activate());
    active=true;find_dvar=find;dvar_text=text;set_string=set;server_running=running;engine_restore=restart;put32(camera,1);
    put32(clients,5);
    snprintf(root,sizeof(root),"build/checkpoint-adapter-%lu",(unsigned long)getpid());
    for (unsigned n=0;n<4;++n) { buffers[n]=(uintptr_t)calloc(1,n<2 ? T7O_CP_SERVER_MAX:T7O_CP_CLIENT_MAX);assert(buffers[n]); }
    t7o_mac_checkpoint_disk_tick();assert(!job.running);
    capture(42);t7o_mac_checkpoint_disk_tick();assert(job.running && job.write);finish_job();t7o_mac_checkpoint_disk_tick();assert(!strcmp(last_status,"saved"));
    char path[4096];assert(t7o_checkpoint_path(root,&current,path,sizeof(path)));T7oCheckpointImage disk={0};assert(t7o_checkpoint_read(path,&disk));assert(u32(disk.payload[0]+4)==42);t7o_checkpoint_dispose(&disk);
    t7o_mac_checkpoint_disk_tick();assert(!job.running); /* No per-frame rewrite. */
    /* Persisted checkpoint survives module teardown and fresh engine buffers. */
    t7o_mac_checkpoint_disk_deactivate();active=true;capture(99);atomic_store(&autorestore,true);atomic_fetch_add(&request_serial,1);
    test_now=2000;t7o_mac_checkpoint_disk_tick();assert(job.running && !job.write);finish_job();t7o_mac_checkpoint_disk_tick();assert(job.running && job.joined && !restore_calls);
    test_now=8000;t7o_mac_checkpoint_disk_tick();assert(waiting_restore && restore_calls==1 && flags[4]);
    assert(u32((uint8_t *)buffers[2]+4)==42 || u32((uint8_t *)buffers[3]+4)==42);
    flags[4]=0;parts=1;t7o_mac_checkpoint_disk_tick();assert(waiting_restore);parts=3;t7o_mac_checkpoint_disk_tick();assert(!waiting_restore);
    /* A rejected trigger rolls back both payloads, engine files and camera. */
    capture(77);accept_restore=false;atomic_fetch_add(&request_serial,1);test_now=9000;t7o_mac_checkpoint_disk_tick();finish_job();test_now=15000;
    unsigned slot=(indices[0]-1)&1;uintptr_t buffer=buffers[slot];uint8_t *saved_files=malloc(sizeof(files));assert(saved_files);memcpy(saved_files,files,sizeof(files));
    t7o_mac_checkpoint_disk_tick();assert(!waiting_restore && u32((uint8_t *)buffer+4)==77 && !memcmp(saved_files,files,sizeof(files)));free(saved_files);
    /* Cancellation or a different map cannot apply the copied save. */
    atomic_fetch_add(&request_serial,1);t7o_mac_checkpoint_disk_tick();finish_job();atomic_store(&autorestore,false);atomic_fetch_add(&request_serial,1);unsigned calls=restore_calls;
    t7o_mac_checkpoint_disk_tick();assert(restore_calls==calls && !job.running);
    host=false;t7o_mac_checkpoint_disk_tick();assert(!job.running);host=true;session=1;t7o_mac_checkpoint_disk_tick();assert(!job.running);session=0x40;
    t7o_mac_checkpoint_disk_deactivate();for (unsigned n=0;n<4;++n) free((void *)buffers[n]);
    unlink(path);char backup[4100];snprintf(backup,sizeof(backup),"%s.bak",path);unlink(backup);char *slash=strrchr(path,'/');assert(slash);*slash=0;rmdir(path);rmdir(root);
    puts("Mac checkpoint disk: paired commits, async snapshots, fresh-session restore, 5-second wait, two-part completion, rejected-trigger rollback and cancellation guards passed (substituted engine)");
}
