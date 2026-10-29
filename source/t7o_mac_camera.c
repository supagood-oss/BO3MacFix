#include "t7o_mac_camera.h"
#include "t7o_mac_offsets.h"
#include "t7o_core.h"
#include "t7o_mac_checkpoint.h"
#include "prepatch.h"
#include "dobby.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
enum { CLIENTS=2,LINKS=4,LINK_OFFSET=0x3272f4,POSE_ENTITY=0x3f8,
       VIEW_ANGLES=0x2d8930,VIEW_ORIGIN=0x131dc8,VIEW_AXIS=0x131de4 };
typedef struct { uint32_t count,entity[LINKS],mode[LINKS];float offset[LINKS][3]; } Links;
_Static_assert(sizeof(Links)==0x54,"Mac camera link fields");
typedef struct {
    void *object;uint8_t *pose;uint8_t *cg;uint32_t entity,flags,time;
    bool has_shader,has_descriptor;uint8_t shader[32],descriptor[48];
} Pending;
typedef struct { uint8_t *cg;uint32_t eligible[LINKS],count,pending_count;Pending pending[LINKS]; } Camera;
/* Memory-file identity is the engine buffer pointer at offset zero. These are
 * the engine's two rotating client checkpoint slots, not a new save format. */
typedef struct { uintptr_t buffer;bool valid;Links links[CLIENTS];uint8_t present; } Saved;
typedef void (*Method)(unsigned,uint64_t,uint64_t);
typedef struct { uint32_t hash,min_args,max_args,pad;Method function;uint32_t kind,pad2; } Builtin;
_Static_assert(sizeof(Builtin)==0x20,"Mac CSC builtin layout");
static uint64_t base;
static bool installed,methods_registered;
static atomic_bool active;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Camera cameras[CLIENTS];static Saved saved[2];static unsigned loading;
static Method originals[3];static uint32_t original_min[3],original_max[3];
static void (*original_draw)(void *,void *,unsigned,unsigned,const void *);
static void (*original_axis)(const float *,float *);
static void (*original_save)(void *),(*original_load)(void *);
static void (*refresh_pose)(int,int);
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static uint32_t u32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static void put32(void *p,uint32_t v) { memcpy(p,&v,4); }
static uintptr_t pointer(const void *p) { uintptr_t v;memcpy(&v,p,sizeof(v));return v; }
static uint8_t *cg_for(unsigned client) {
    uint8_t *p=(void *)pointer(address(T7O_MAC_CG_ARRAY));
    return p && client<CLIENTS && client<u32(address(T7O_MAC_CG_COUNT)) ? p+(size_t)client*T7O_MAC_CG_STRIDE : NULL;
}
static bool links_for(uint8_t *cg,Links *out) {
    if (!cg) return false;
    memcpy(out,cg+LINK_OFFSET,sizeof(*out));return out->count<=LINKS;
}
static void reset_camera(Camera *camera,uint8_t *cg) {
    if (camera->cg!=cg) { memset(camera,0,sizeof(*camera));camera->cg=cg; }
}
static void eligible(Camera *camera,uint8_t *cg) {
    Links links;reset_camera(camera,cg);camera->count=0;
    if (links_for(cg,&links)) {
        camera->count=links.count;memcpy(camera->eligible,links.entity,links.count*4);
    }
}
static bool contains(const uint32_t *ids,unsigned count,unsigned entity) {
    for (unsigned i=0;i<count;++i) if (ids[i]==entity) return true;
    return false;
}
/* Mirrors the native operation order, including bitwise angle comparisons and
 * the distinction between modes 0/1/2 and the refdef-axis modes. */
static bool transform(uint8_t *cg,uint8_t *pose,unsigned index,const Links *links) {
    float angles[3],origin[3],axis[9];
    memcpy(angles,pose+0x4c,sizeof(angles));uint32_t mode=links->mode[index];
    if (mode<3) {
        memcpy(&angles[mode],cg+VIEW_ANGLES+mode*4,4);original_axis(angles,axis);
    } else {
        if (mode!=5) memcpy(angles,cg+VIEW_ANGLES,sizeof(angles));
        memcpy(axis,cg+VIEW_AXIS,sizeof(axis));
    }
    float camera_origin[3];memcpy(camera_origin,cg+VIEW_ORIGIN,sizeof(camera_origin));
    for (unsigned i=0;i<3;++i) {
        origin[i]=links->offset[index][0]*axis[i]+camera_origin[i];
        origin[i]+=links->offset[index][1]*axis[3+i];
        origin[i]+=links->offset[index][2]*axis[6+i];
        if (!isfinite(origin[i]) || !isfinite(angles[i])) return false;
    }
    bool changed=memcmp(angles,pose+0x4c,sizeof(angles))!=0;
    float previous[3];memcpy(previous,pose+0x40,sizeof(previous));
    for (unsigned i=0;i<3;++i) changed|=previous[i]!=origin[i];
    if (!changed) return false;
    memcpy(pose+0x40,origin,sizeof(origin));memcpy(pose+0x4c,angles,sizeof(angles));return true;
}
static void draw(void *object,void *pose_arg,unsigned entity,unsigned flags,const void *shader) {
    uint8_t *pose=pose_arg;bool queued=false;
    if (atomic_load(&active) && pose && object && pose[2]<CLIENTS) {
        unsigned client=pose[2];uint8_t *cg=cg_for(client);Links links;
        pthread_mutex_lock(&lock);Camera *camera=&cameras[client];reset_camera(camera,cg);
        if (!loading && links_for(cg,&links) && u32(pose+POSE_ENTITY)==entity &&
            contains(camera->eligible,camera->count,entity) && contains(links.entity,links.count,entity)) {
            queued=true; /* Native queue capacity is four; an excess draw is discarded. */
            unsigned i=0;while (i<camera->pending_count && camera->pending[i].pose!=pose) ++i;
            if (i<LINKS) {
                Pending *pending=&camera->pending[i];memset(pending,0,sizeof(*pending));
                pending->object=object;pending->pose=pose;pending->cg=cg;pending->entity=entity;
                pending->flags=flags;pending->time=u32(cg+T7O_MAC_CG_TIME);pending->has_shader=shader!=NULL;
                if (shader) {
                    memcpy(pending->shader,shader,32);uintptr_t descriptor=pointer((const uint8_t *)shader+24);
                    if ((pending->shader[0]&8) && descriptor) {
                        pending->has_descriptor=true;memcpy(pending->descriptor,(const void *)descriptor,48);
                    }
                }
                if (i==camera->pending_count) ++camera->pending_count;
            }
        }
        pthread_mutex_unlock(&lock);
    }
    if (!queued) original_draw(object,pose,entity,flags,shader);
}
static void angles_to_axis(const float *angles,float *axis) {
    original_axis(angles,axis);
    if (!atomic_load(&active)) return;
    for (unsigned client=0;client<CLIENTS;++client) {
        uint8_t *cg=cg_for(client);
        /* The Mac view calculation tail-calls AnglesToAxis. Check both exact
         * CG fields instead of relying on a Windows caller return address. */
        if (!cg || angles!=(const float *)(cg+VIEW_ANGLES) || axis!=(float *)(cg+VIEW_AXIS)) continue;
        Pending pending[LINKS];unsigned count=0;Links links;
        pthread_mutex_lock(&lock);Camera *camera=&cameras[client];eligible(camera,cg);
        bool valid_links=links_for(cg,&links);
        if (!loading) {
            count=camera->pending_count;memcpy(pending,camera->pending,count*sizeof(*pending));
        }
        camera->pending_count=0;pthread_mutex_unlock(&lock);
        for (unsigned i=0;i<count;++i) {
            Pending *p=&pending[i];
            if (p->cg!=cg || p->time!=u32(cg+T7O_MAC_CG_TIME) || !p->object || !p->pose ||
                u32(p->pose+POSE_ENTITY)!=p->entity) continue;
            unsigned at=0;while (valid_links && at<links.count && links.entity[at]!=p->entity) ++at;
            if (valid_links && at<links.count && transform(cg,p->pose,at,&links)) refresh_pose((int)client,(int)p->entity);
            if (p->has_descriptor) { uintptr_t descriptor=(uintptr_t)p->descriptor;memcpy(p->shader+24,&descriptor,8); }
            original_draw(p->object,p->pose,p->entity,p->flags,p->has_shader ? p->shader : NULL);
        }
        break;
    }
}
static void remove_entity(unsigned client,unsigned entity) {
    uint8_t *cg=cg_for(client);Links links;
    pthread_mutex_lock(&lock);Camera *camera=&cameras[client];reset_camera(camera,cg);
    if (links_for(cg,&links)) {
        unsigned kept=0;
        for (unsigned i=0;i<links.count;++i) if (links.entity[i]!=entity) {
            links.entity[kept]=links.entity[i];links.mode[kept]=links.mode[i];
            memcpy(links.offset[kept],links.offset[i],12);++kept;
        }
        if (kept!=links.count) {
            for (unsigned i=kept;i<LINKS;++i) { links.entity[i]=links.mode[i]=0;memset(links.offset[i],0,12); }
            links.count=kept;memcpy(cg+LINK_OFFSET,&links,sizeof(links));
        }
        eligible(camera,cg);
    }
    /* Drop references before the engine deletes their entities. */
    unsigned kept=0;
    for (unsigned i=0;i<camera->pending_count;++i)
        if (camera->pending[i].entity!=entity) camera->pending[kept++]=camera->pending[i];
    camera->pending_count=kept;pthread_mutex_unlock(&lock);
}
static void method(unsigned which,unsigned instance,uint64_t low,uint64_t high) {
    unsigned client=(unsigned)(high>>32);
    if (atomic_load(&active) && which<2 && client<CLIENTS && !(high&0xffff)) remove_entity(client,(unsigned)low);
    /* Mac passes the 16-byte CSC entity reference in RSI/RDX by value. */
    originals[which](instance,low,high);
    if (atomic_load(&active) && which==2 && client<CLIENTS) {
        pthread_mutex_lock(&lock);eligible(&cameras[client],cg_for(client));pthread_mutex_unlock(&lock);
    }
}
static void delete_entity(unsigned i,uint64_t l,uint64_t h) { method(0,i,l,h); }
static void force_delete(unsigned i,uint64_t l,uint64_t h) { method(1,i,l,h); }
static void link_to_camera(unsigned i,uint64_t l,uint64_t h) { method(2,i,l,h); }
static Method const methods[]={delete_entity,force_delete,link_to_camera};
static const uint32_t rows[]={T7O_MAC_CSC_DELETE_ROW,T7O_MAC_CSC_FORCE_DELETE_ROW,T7O_MAC_CSC_CAMERA_ROW};
static int save_slot(void *file) {
    uintptr_t p=(uintptr_t)file,start=(uintptr_t)address(T7O_MAC_CHECKPOINT_FILES)+0x80070;
    return p==start ? 0 : p==start+0x40038 ? 1 : -1;
}
static void save_client(void *file) {
    if (atomic_load(&active)) {
        int slot=save_slot(file);
        if (slot>=0) {
            pthread_mutex_lock(&lock);Saved *s=&saved[slot];memset(s,0,sizeof(*s));s->buffer=pointer(file);
            for (unsigned c=0;c<CLIENTS;++c) if (links_for(cg_for(c),&s->links[c])) s->present|=1u<<c;
            s->valid=s->buffer!=0;pthread_mutex_unlock(&lock);
        }
    }
    original_save(file);
}
static void load_client(void *file) {
    t7o_mac_checkpoint_client_load_begin(file);
    int slot=atomic_load(&active) ? save_slot(file) : -1;
    if (slot<0) { original_load(file);t7o_mac_checkpoint_client_load_end(file);return; }
    pthread_mutex_lock(&lock);++loading;memset(cameras,0,sizeof(cameras));
    for (unsigned c=0;c<CLIENTS;++c) { uint8_t *cg=cg_for(c);if (cg) put32(cg+LINK_OFFSET,0); }
    pthread_mutex_unlock(&lock);
    original_load(file);
    pthread_mutex_lock(&lock);Saved *s=&saved[slot];
    for (unsigned c=0;c<CLIENTS;++c) {
        uint8_t *cg=cg_for(c);if (!cg) continue;
        if (s->valid && s->buffer==pointer(file) && (s->present&(1u<<c))) memcpy(cg+LINK_OFFSET,&s->links[c],sizeof(Links));
        else put32(cg+LINK_OFFSET,0);
        eligible(&cameras[c],cg);
    }
    --loading;pthread_mutex_unlock(&lock);
    t7o_mac_checkpoint_client_load_end(file);
}
void t7o_mac_camera_deactivate(void) {
    atomic_store(&active,false);
    if (methods_registered) for (unsigned i=0;i<3;++i) {
        Builtin *row=address(rows[i]);
        if (row->function==methods[i]) { row->function=originals[i];row->min_args=original_min[i];row->max_args=original_max[i]; }
    }
    methods_registered=false;pthread_mutex_lock(&lock);
    memset(cameras,0,sizeof(cameras));memset(saved,0,sizeof(saved));loading=0;pthread_mutex_unlock(&lock);
}
bool t7o_mac_camera_activate(void) {
    if (!installed) return false;
    if (atomic_load(&active)) return true;
    static const char *const names[]={"delete","forcedelete","linktocamera"};
    static const uint32_t callbacks[]={T7O_MAC_CSC_DELETE,T7O_MAC_CSC_FORCE_DELETE,T7O_MAC_CSC_CAMERA};
    for (unsigned i=0;i<3;++i) {
        Builtin *row=address(rows[i]);
        if (row->hash!=t7o_script_hash(names[i]) || row->function!=(Method)address(callbacks[i]) ||
            row->min_args!=(i==2 ? 1u : 0u) || row->max_args!=(i==2 ? 2u : 0u)) return false;
    }
    for (unsigned i=0;i<3;++i) {
        Builtin *row=address(rows[i]);originals[i]=row->function;original_min[i]=row->min_args;original_max[i]=row->max_args;
        row->function=methods[i];if (i==2) row->min_args=0;
    }
    methods_registered=true;atomic_store(&active,true);return true;
}
bool t7o_mac_camera_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint32_t functions[]={T7O_MAC_CAMERA_DRAW,T7O_MAC_ANGLES_TO_AXIS,T7O_MAC_CAMERA_REFRESH,T7O_MAC_CHECKPOINT_CLIENT_SAVE,T7O_MAC_CHECKPOINT_CLIENT_LOAD};
    const uint8_t prologue[]={0x55,0x48,0x89,0xe5};
    for (unsigned i=0;i<sizeof(functions)/sizeof(functions[0]);++i)
        if (memcmp(prepatch_original_code(address(functions[i]),4),prologue,4)) return false;
    refresh_pose=(void *)address(T7O_MAC_CAMERA_REFRESH);
    if (DobbyHook(address(T7O_MAC_CAMERA_DRAW),(void *)draw,(void **)&original_draw) ||
        DobbyHook(address(T7O_MAC_ANGLES_TO_AXIS),(void *)angles_to_axis,(void **)&original_axis) ||
        DobbyHook(address(T7O_MAC_CHECKPOINT_CLIENT_SAVE),(void *)save_client,(void **)&original_save) ||
        DobbyHook(address(T7O_MAC_CHECKPOINT_CLIENT_LOAD),(void *)load_client,(void **)&original_load)) return false;
    installed=true;return true;
}
