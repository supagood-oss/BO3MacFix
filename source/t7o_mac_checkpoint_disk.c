#include "t7o_mac_checkpoint_disk.h"
#include "t7o_mac_checkpoint.h"
#include "t7o_checkpoint_store.h"
#include "t7o_mac_camera.h"
#include "t7o_mac_offsets.h"
#include "prepatch.h"
#include "mod_fileio.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef T7O_CHECKPOINT_OWNER
#include "steam.h"
#define T7O_CHECKPOINT_OWNER() (steam_id().id)
#endif
static uint64_t base;
static bool installed,commands_registered;
static atomic_bool active;
static pthread_mutex_t disk_lock=PTHREAD_MUTEX_INITIALIZER;
static void *(*find_dvar)(const char *),*(*thread_context)(void);
static const char *(*dvar_text)(const void *);
static void (*set_string)(const char *,const char *,bool),(*command_add)(const char *,void (*)(void),void *);
static void *(*command_find)(const char *);
static bool (*server_running)(void),(*engine_restore)(void);
static _Alignas(8) uint8_t command_records[2][0x30];
static atomic_uint selected_slot=1,request_serial;
static atomic_bool autorestore;
static unsigned handled_serial;
static char root[4096];
static char reported_status[128];
static uint32_t written_indices[2];
static bool have_written;
static bool capture_seen[2];
static uint32_t capture_indices[2];
static T7oCheckpointIdentity current;
static T7oCheckpointIdentity capture_identity[2];
static uint64_t stable_since;
static uint32_t stable_indices[2];
static bool waiting_restore;
static uint64_t restore_started;
static uint32_t restore_indices[2];
/* The worker owns only copied bytes and filesystem paths. All dvar, camera and
 * engine operations run on the existing checkpoint frame's game thread. */
typedef struct { pthread_t thread;atomic_bool done;bool running,joined,write,ok;char path[4096];T7oCheckpointImage image;unsigned serial; } Job;
static Job job;
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t u32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uintptr_t pointer(const void *p) { uintptr_t v;memcpy(&v,p,8);return v; }
static void put32(void *p,uint32_t v) { memcpy(p,&v,4); }
#ifndef T7O_CHECKPOINT_MILLISECONDS
static uint64_t milliseconds(void) {
    struct timespec ts;if (clock_gettime(CLOCK_MONOTONIC,&ts)) return 0;
    return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
}
#define T7O_CHECKPOINT_MILLISECONDS() milliseconds()
#endif
static const char *setting(const char *name) { void *d=find_dvar(name);return d ? dvar_text(d):NULL; }
static bool copy_text(char *out,size_t size,const char *text) {
    if (!text || strlen(text)>=size) return false;
    memset(out,0,size);memcpy(out,text,strlen(text));return true;
}
static void status(const char *value) {
    if (!strcmp(reported_status,value)) return;
    snprintf(reported_status,sizeof(reported_status),"%s",value);
    if (set_string) set_string("bo3macfix_checkpoint_status",value,true);
    printf("BO3MacFix checkpoint: %s\n",value);
}
static bool context(T7oCheckpointIdentity *i) {
    if (!active || !server_running() || (u32(address(T7O_MAC_SESSION_FLAGS))&0x3cf)!=0x40) return false;
    memset(i,0,sizeof(*i));i->owner=T7O_CHECKPOINT_OWNER();i->slot=atomic_load(&selected_slot);
    const char *max=setting("com_maxclients");char *end=NULL;long clients=max ? strtol(max,&end,10):0;
    if (!max || !*max || *end || clients<1 || clients>18) return false;
    i->max_clients=(uint32_t)clients;
    return copy_text(i->map,sizeof(i->map),setting("mapname")) && strcmp(i->map,"core_frontend") &&
        copy_text(i->mod,sizeof(i->mod),setting("fs_game")) && copy_text(i->folder,sizeof(i->folder),setting("aae_savegame_folder")) &&
        t7o_checkpoint_identity_valid(i);
}
static bool idle(void) {
    const uint8_t *f=address(T7O_MAC_CHECKPOINT_FLAGS);
    for (unsigned i=0;i<6;++i) if (f[i]) return false;
    return true;
}
static bool players_ready(unsigned count) {
    const uint8_t *clients=(void *)pointer(address(T7O_MAC_SV_CLIENTS));bool any=false;
    if (!clients || count<1 || count>18) return false;
    for (unsigned c=0;c<count;++c) {
        unsigned state=u32(clients+(size_t)c*T7O_MAC_SV_CLIENT_STRIDE);
        if (state>2 && state!=5) return false;
        any|=state==5;
    }
    return any;
}
static bool committed(uint32_t indices[2],unsigned slots[2],uint32_t lengths[2]) {
    const uint8_t *files=address(T7O_MAC_CHECKPOINT_FILES),*sizes=address(T7O_MAC_CHECKPOINT_SIZES);
    if (!idle()) return false;
    for (unsigned c=0;c<2;++c) {
        indices[c]=u32((uint8_t *)address(T7O_MAC_CHECKPOINT_INDICES)+c*4);slots[c]=((indices[c]-1)&1)+c*2;
        if (u32((uint8_t *)address(T7O_MAC_CHECKPOINT_STATES)+slots[c]*4)!=2) return false;
        const uint8_t *file=files+slots[c]*0x40038;
        uintptr_t buffer=pointer((uint8_t *)address(T7O_MAC_CHECKPOINT_BUFFERS)+slots[c]*8);
        lengths[c]=u32(file+0xc);
        if (!buffer || pointer(file)!=buffer || u32(file+0x1c)!=UINT32_MAX || file[0x2d] || lengths[c]<4 ||
            lengths[c]>u32(sizes+c*4) || lengths[c]>(c ? T7O_CP_CLIENT_MAX:T7O_CP_SERVER_MAX)) return false;
    }
    return true;
}
static const char *argument(unsigned *count) {
    uint8_t *thread=thread_context(),*args=thread ? (void *)pointer(thread+0x18):NULL;
    if (!args) return NULL;
    unsigned depth=u32(args);if (depth>=8) return NULL;
    *count=u32(args+0x64+depth*4);if (*count!=2) return NULL;
    const uint8_t *argv=(void *)pointer(args+0x88+depth*8);
    return argv ? (void *)pointer(argv+8):NULL;
}
static void command_slot(void) {
    if (!active) return;
    unsigned count;const char *v=argument(&count);
    if (!v || v[0]<'1' || v[0]>'6' || v[1]) return;
    atomic_store(&selected_slot,(unsigned)(v[0]-'0'));atomic_fetch_add(&request_serial,1);
}
static void command_autorestore(void) {
    if (!active) return;
    unsigned count;const char *v=argument(&count);
    if (!v || (v[0]!='0' && v[0]!='1') || v[1]) return;
    atomic_store(&autorestore,v[0]=='1');atomic_fetch_add(&request_serial,1);
}
static void *file_worker(void *unused) {
    (void)unused;
    if (job.write) {
        char directory[4096];memcpy(directory,job.path,strlen(job.path)+1);char *slash=strrchr(directory,'/');
        if (slash) *slash=0;
        job.ok=slash && bo3_mkdirs(directory) && t7o_checkpoint_write(job.path,&job.image);
    } else job.ok=t7o_checkpoint_read(job.path,&job.image);
    atomic_store(&job.done,true);return NULL;
}
static bool start_job(bool write,const T7oCheckpointIdentity *identity) {
    if (!root[0] || job.running || !t7o_checkpoint_path(root,identity,job.path,sizeof(job.path))) return false;
    job.write=write;job.ok=false;job.joined=false;job.serial=atomic_load(&request_serial);atomic_store(&job.done,false);
    if (pthread_create(&job.thread,NULL,file_worker,NULL)) return false;
    job.running=true;return true;
}
static void dispose_job(void) { t7o_checkpoint_dispose(&job.image);job.running=false; }
static void date(unsigned slot,uint64_t timestamp) {
    char name[32],text[32]="";time_t value=(time_t)timestamp;struct tm tm;
#ifdef _WIN32
    bool valid=localtime_s(&tm,&value)==0;
#else
    bool valid=localtime_r(&value,&tm)!=NULL;
#endif
    if (timestamp && valid) strftime(text,sizeof(text),"%Y-%m-%d",&tm);
    snprintf(name,sizeof(name),"aae_savegame_date_%u",slot);set_string(name,text,true);
}
/* Headers are small; slot refresh performs no engine writes or payload reads.
 * Full checksums and chunk validation are repeated by the read worker. */
static void refresh_dates(T7oCheckpointIdentity identity) {
    for (unsigned n=1;n<=6;++n) {
        identity.slot=n;char path[4096];uint64_t stamp=0;
        if (t7o_checkpoint_path(root,&identity,path,sizeof(path))) {
            FILE *f=fopen(path,"rb");uint8_t h[T7O_CP_HEADER];T7oCheckpointImage image;
            if (f) {
                if (fread(h,1,sizeof(h),f)==sizeof(h) && t7o_checkpoint_decode(h,&image) &&
                    t7o_checkpoint_identity_equal(&identity,&image.identity)) stamp=image.timestamp;
                fclose(f);
            }
        }
        date(n,stamp);
    }
}
static bool copy_committed(const T7oCheckpointIdentity *identity,const unsigned slots[2],const uint32_t lengths[2]) {
    T7oCheckpointImage *image=&job.image;memset(image,0,sizeof(*image));image->identity=*identity;image->timestamp=(uint64_t)time(NULL);
    for (unsigned c=0;c<2;++c) {
        const void *buffer=(void *)pointer((uint8_t *)address(T7O_MAC_CHECKPOINT_BUFFERS)+slots[c]*8);
        if (!t7o_checkpoint_chunks_valid(buffer,lengths[c])) goto fail;
        image->size[c]=lengths[c];image->payload[c]=malloc(lengths[c]);if (!image->payload[c]) goto fail;
        memcpy(image->payload[c],buffer,lengths[c]);
    }
    t7o_mac_camera_checkpoint_export(slots[1]-2,pointer((uint8_t *)address(T7O_MAC_CHECKPOINT_BUFFERS)+slots[1]*8),image->camera);
    if (t7o_mac_camera_checkpoint_valid(image->camera)) return true;
fail:t7o_checkpoint_dispose(image);return false;
}
/* Rollback is limited to a rejected trigger, before any engine load. Once the
 * engine has accepted a restart, it owns the world and network transaction. */
static bool apply_image(const T7oCheckpointIdentity *identity,const unsigned slots[2]) {
    if (!t7o_checkpoint_identity_equal(identity,&job.image.identity) || !t7o_mac_camera_checkpoint_valid(job.image.camera) || !idle()) return false;
    uint8_t *files=address(T7O_MAC_CHECKPOINT_FILES),*states=address(T7O_MAC_CHECKPOINT_STATES);
    uint8_t *backups[2]={0},*file_backup=malloc(0x1000e0);uint8_t state_backup[16],camera_backup[T7O_CP_CAMERA];bool ok=false;
    uint32_t capacities[2]={u32(address(T7O_MAC_CHECKPOINT_SIZES)),u32((uint8_t *)address(T7O_MAC_CHECKPOINT_SIZES)+4)};
    if (!file_backup) return false;
    for (unsigned c=0;c<2;++c) {
        if (capacities[c]>(c ? T7O_CP_CLIENT_MAX:T7O_CP_SERVER_MAX) || capacities[c]<job.image.size[c]) goto done;
        backups[c]=malloc(capacities[c]);if (!backups[c]) goto done;
        memcpy(backups[c],(void *)pointer((uint8_t *)address(T7O_MAC_CHECKPOINT_BUFFERS)+slots[c]*8),capacities[c]);
    }
    memcpy(file_backup,files,0x1000e0);memcpy(state_backup,states,16);
    uintptr_t client_buffer=pointer((uint8_t *)address(T7O_MAC_CHECKPOINT_BUFFERS)+slots[1]*8);
    t7o_mac_camera_checkpoint_export(slots[1]-2,client_buffer,camera_backup);
    for (unsigned c=0;c<2;++c) {
        void *buffer=(void *)pointer((uint8_t *)address(T7O_MAC_CHECKPOINT_BUFFERS)+slots[c]*8);
        memcpy(buffer,job.image.payload[c],job.image.size[c]);memset((uint8_t *)buffer+job.image.size[c],0,capacities[c]-job.image.size[c]);
        memset(files+slots[c]*0x40038,0,0x40038);put32(states+slots[c]*4,2);
    }
    t7o_mac_camera_checkpoint_import(slots[1]-2,client_buffer,job.image.camera);
    t7o_mac_checkpoint_restore_prepare();ok=engine_restore();
    if (!ok) {
        t7o_mac_checkpoint_restore_cancel();
        for (unsigned c=0;c<2;++c) memcpy((void *)pointer((uint8_t *)address(T7O_MAC_CHECKPOINT_BUFFERS)+slots[c]*8),backups[c],capacities[c]);
        memcpy(files,file_backup,0x1000e0);memcpy(states,state_backup,16);
        t7o_mac_camera_checkpoint_import(slots[1]-2,client_buffer,camera_backup);
    }
done:free(backups[0]);free(backups[1]);free(file_backup);return ok;
}
static void tick(void) {
    if (!active) return;
    T7oCheckpointIdentity identity;bool valid=context(&identity);
    if (!valid) { stable_since=0;return; }
    uint64_t now=T7O_CHECKPOINT_MILLISECONDS();
    if (!t7o_checkpoint_identity_equal(&identity,&current)) {
        current=identity;have_written=false;stable_since=0;waiting_restore=false;refresh_dates(identity);
    }
    uint32_t indices[2],lengths[2];unsigned slots[2];bool ready=committed(indices,slots,lengths) && players_ready(identity.max_clients);
    if (!ready) stable_since=0;
    else if (!stable_since || memcmp(stable_indices,indices,sizeof(indices))) { stable_since=now;memcpy(stable_indices,indices,sizeof(indices)); }
    if (waiting_restore) {
        if (!idle() || t7o_mac_checkpoint_restore_parts()!=3) {
            if (now-restore_started>30000) status("restore incomplete; restart the map before another attempt");
            return;
        }
        /* The normal loader consumes the pending flag, then restores server
         * and client before the next fresh committed checkpoint is created. */
        waiting_restore=false;have_written=true;memcpy(written_indices,restore_indices,sizeof(written_indices));status("restore completed; verify co-op state");
    }
    if (job.running) {
        if (!atomic_load(&job.done)) return;
        if (!job.joined) { pthread_join(job.thread,NULL);job.joined=true; }
        job.running=false;
        if (job.write) {
            if (job.ok) {
                if (t7o_checkpoint_identity_equal(&identity,&job.image.identity)) date(job.image.identity.slot,job.image.timestamp);
                status("saved");
            } else status("save failed; previous file retained; retry at next checkpoint");
            dispose_job();
        } else {
            if (!job.ok || job.serial!=atomic_load(&request_serial) || !t7o_checkpoint_identity_equal(&identity,&job.image.identity)) {
                status(job.ok ? "restore cancelled; context changed":"no valid save in selected slot");handled_serial=job.serial;
                if (ready) { have_written=true;memcpy(written_indices,indices,sizeof(indices)); }
                dispose_job();return;
            }
            if (!ready || !stable_since || now-stable_since<5000) { job.running=true;return; }
            /* The thread is already joined. Keep its result pending without
             * joining twice while waiting for the engine's checkpoint pair. */
            bool restored=apply_image(&identity,slots);handled_serial=job.serial;
            if (restored) { waiting_restore=true;restore_started=now;memcpy(restore_indices,indices,sizeof(indices));status("restoring"); }
            else { have_written=true;memcpy(written_indices,indices,sizeof(indices));status("restore rejected; current checkpoint retained"); }
            dispose_job();return;
        }
    }
    if (!ready) { stable_since=0;return; }
    if (!stable_since) stable_since=now;
    unsigned serial=atomic_load(&request_serial);
    if (atomic_load(&autorestore) && serial!=handled_serial) {
        if (!start_job(false,&identity)) {
            handled_serial=serial;have_written=true;memcpy(written_indices,indices,sizeof(indices));status("could not read save; current file retained");
        }
        else status("loading selected save");
        return;
    }
    if (waiting_restore || !capture_seen[0] || !capture_seen[1] || !t7o_checkpoint_identity_equal(&identity,&capture_identity[0]) ||
        !t7o_checkpoint_identity_equal(&identity,&capture_identity[1]) || indices[0]!=capture_indices[0]+1 || indices[1]!=capture_indices[1]+1 ||
        (have_written && (written_indices[0]==indices[0] || written_indices[1]==indices[1]))) return;
    if (copy_committed(&identity,slots,lengths) && start_job(true,&identity)) {
        memcpy(written_indices,indices,sizeof(indices));have_written=true;status("saving");
    } else { dispose_job();status("checkpoint not ready for disk save"); }
}
void t7o_mac_checkpoint_disk_tick(void) { pthread_mutex_lock(&disk_lock);tick();pthread_mutex_unlock(&disk_lock); }
void t7o_mac_checkpoint_disk_frame_begin(int channel) {
    if (channel<0 || channel>1) return;
    pthread_mutex_lock(&disk_lock);
    if (active && ((const uint8_t *)address(T7O_MAC_CHECKPOINT_FLAGS))[channel]) {
        capture_seen[channel]=context(&capture_identity[channel]);capture_indices[channel]=u32((uint8_t *)address(T7O_MAC_CHECKPOINT_INDICES)+channel*4);
    }
    pthread_mutex_unlock(&disk_lock);
}
void t7o_mac_checkpoint_disk_deactivate(void) {
    pthread_mutex_lock(&disk_lock);active=false;if (job.running && !job.joined) pthread_join(job.thread,NULL);dispose_job();memset(&current,0,sizeof(current));
    stable_since=0;have_written=false;waiting_restore=false;
    memset(capture_seen,0,sizeof(capture_seen));
    pthread_mutex_unlock(&disk_lock);
}
static bool activate(void) {
    if (!installed) return false;
    if (!commands_registered) {
        if (command_find("zm_savegame_slot") || command_find("zm_savegame_autorestore")) return false;
        command_add("zm_savegame_slot",command_slot,command_records[0]);command_add("zm_savegame_autorestore",command_autorestore,command_records[1]);commands_registered=true;
    }
    if (!setting("aae_savegame_folder")) set_string("aae_savegame_folder","_NativeCoreTest",true);
    const char *home=getenv("HOME");root[0]=0;
    if (home && snprintf(root,sizeof(root),"%s/Library/Application Support/BO3MacFix/AAESavegames",home)>=(int)sizeof(root)) root[0]=0;
    active=true;memset(&current,0,sizeof(current));have_written=false;stable_since=0;waiting_restore=false;
    memset(capture_seen,0,sizeof(capture_seen));
    if (atomic_load(&autorestore)) atomic_fetch_add(&request_serial,1);
    reported_status[0]=0;status("ready");return true;
}
bool t7o_mac_checkpoint_disk_activate(void) { pthread_mutex_lock(&disk_lock);bool ok=activate();pthread_mutex_unlock(&disk_lock);return ok; }
bool t7o_mac_checkpoint_disk_install(uint64_t game_base) {
    if (installed) return base==game_base;
    base=game_base;
    /* The trigger's original checks, flag write and fastrestart call are kept
     * intact. It does not need another hook or a replacement network packet. */
    const uint8_t prefix[]={0x48,0x83,0x3d,0xa7,0x81,0x9b,0x13,0x00};
    if (memcmp(prepatch_original_code(address(T7O_MAC_CHECKPOINT_RESTART),sizeof(prefix)),prefix,sizeof(prefix))) return false;
    find_dvar=address(T7O_MAC_DVAR_FIND_NAME);dvar_text=address(T7O_MAC_DVAR_VALUE_STRING);set_string=address(T7O_MAC_SET_DVAR_STRING);
    command_add=address(T7O_MAC_COMMAND_ADD);command_find=address(T7O_MAC_COMMAND_FIND);thread_context=address(T7O_MAC_THREAD_CONTEXT);
    server_running=address(T7O_MAC_SERVER_RUNNING);engine_restore=address(T7O_MAC_CHECKPOINT_RESTART);installed=true;return true;
}
