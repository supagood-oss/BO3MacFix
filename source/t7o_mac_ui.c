#include "t7o_mac_ui.h"
#include "t7o_mac_offsets.h"
#include "t7o_inline.h"
#include "prepatch.h"
#include "dobby.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#ifndef T7O_DEV_NATIVE
#define T7O_DEV_NATIVE 0
#endif
enum { TAGS=128,WIDGETS=1024,CLIENTS=2,REGISTRY=-10000 };
typedef void (*Layout)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
typedef struct { bool valid;float position[3],quaternion[4]; } Sample;
typedef struct { char name[256];unsigned id;Sample sample[CLIENTS]; } Tag;
typedef struct {
    void *element;unsigned tag;float offset[3];bool line,failed,in_callback;
    lua_State *vm;void *global;int callback_ref,table_ref;
    HksObject callback,arguments;Layout previous;
} Widget;
typedef struct Method { const char *name;lua_CFunction function;struct Method *next; } Method;
static uint64_t base;
static bool installed,methods_registered;
static atomic_bool active;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Tag tags[TAGS];static Widget widgets[WIDGETS];
static unsigned tag_count,widget_count;
static _Atomic(uintptr_t) objects[CLIENTS];static void *models[CLIENTS];
static void *(*original_matrices)(void *);
static int (*bone_index)(void *,unsigned,uint16_t *,unsigned);
static unsigned (*intern_string)(const char *,unsigned,unsigned,unsigned);
static void *(*element_arg)(lua_State *,int);
static void (*create_table)(lua_State *,int,int);
static int (*reference)(lua_State *,int);
static void (*unreference)(lua_State *,int,int);
static void (*project)(int,const float *,float *);
static Layout generic_layout;
#ifndef T7O_MAC_ADDRESS
#define T7O_MAC_ADDRESS(offset) ((void *)(uintptr_t)(base+(offset)))
#endif
static void *address(uint32_t offset) { return T7O_MAC_ADDRESS(offset); }
static void *pointer(const void *p) { void *v;memcpy(&v,p,sizeof(v));return v; }
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uint16_t read16(const void *p) { uint16_t v;memcpy(&v,p,2);return v; }
static bool bounded(float value,float maximum) { return isfinite(value) && fabsf(value)<maximum; }
static Widget *find_widget(void *element) {
    for (unsigned i=0;i<widget_count;++i) if (widgets[i].element==element) return &widgets[i];
    return NULL;
}
static bool world_position(unsigned client,const Widget *widget,float out[3]) {
    if (client>=CLIENTS || widget->tag>=tag_count) return false;
    Sample value=tags[widget->tag].sample[client];if (!value.valid) return false;
    float x=widget->offset[0],y=widget->offset[1],z=widget->offset[2];
    float qx=value.quaternion[0],qy=value.quaternion[1],qz=value.quaternion[2],qw=value.quaternion[3];
    float norm=qx*qx+qy*qy+qz*qz+qw*qw;
    if (!(norm>0.25f && norm<4)) return false;
    float a=qy*z-qz*y,b=qz*x-qx*z,c=qx*y-qy*x,scale=2/norm;
    out[0]=((c*qy-b*qz)+a*qw)*scale+x+value.position[0];
    out[1]=((a*qz-c*qx)+b*qw)*scale+y+value.position[1];
    out[2]=((b*qx-a*qy)+c*qw)*scale+z+value.position[2];
    return bounded(out[0],10000000) && bounded(out[1],10000000) && bounded(out[2],10000000);
}
static bool client_ready(unsigned client) {
    return client<CLIENTS && client<read32(address(T7O_MAC_CG_COUNT)) && pointer(address(T7O_MAC_CG_ARRAY));
}
static void layout_basic(uintptr_t client,uintptr_t element,uintptr_t root,uintptr_t a,uintptr_t b,uintptr_t vm) {
    if (!atomic_load(&active)) { generic_layout(client,element,root,a,b,vm);return; }
    float point[3];pthread_mutex_lock(&lock);Widget *widget=find_widget((void *)element);
    bool valid=widget && world_position((unsigned)client,widget,point);pthread_mutex_unlock(&lock);
    if (valid && client_ready((unsigned)client)) {
        memcpy((uint8_t *)element+0x78,point,sizeof(point));generic_layout(client,element,root,a,b,vm);
    }
}
static void layout_line(uintptr_t client,uintptr_t element,uintptr_t root,uintptr_t a,uintptr_t b,uintptr_t vm) {
    Widget copy;float point[3];bool valid=false;
    pthread_mutex_lock(&lock);Widget *widget=find_widget((void *)element);
    if (!widget) { pthread_mutex_unlock(&lock);return; }
    copy=*widget;
    if (atomic_load(&active)) valid=world_position((unsigned)client,widget,point);
    pthread_mutex_unlock(&lock);
    if (copy.previous && copy.previous!=layout_line) copy.previous(client,element,root,a,b,vm);
    if (!atomic_load(&active) || copy.vm!=(lua_State *)vm || copy.global!=HKS_GLOBAL(copy.vm) ||
        copy.failed || copy.in_callback) return;
    float screen[4]={NAN,NAN,NAN,NAN};
    if (valid && client_ready((unsigned)client)) project((int)client,point,screen);
    valid=valid && bounded(screen[0],10000000) && bounded(screen[1],10000000);
    lua_State *s=copy.vm;ptrdiff_t top=HKS_TOP(s)-HKS_BASE(s);
    /* Registry references root both objects. Numeric array updates need no GC
     * pointer barrier, and no Windows allocator or raw HKS table is fabricated. */
    HKS_TOP(s)[0]=copy.callback;
    if (valid && (copy.arguments.t&15)==5 && copy.arguments.v.ptr) {
        uint8_t *table=copy.arguments.v.ptr;HksObject *array=pointer(table+0x28);
        uint32_t size=read32(table+0x30);
        if (!array || size<2 || size>0xfffff) valid=false;
        else for (unsigned i=0;i<2;++i) { array[i].t=HKS_TNUMBER;array[i].v.number=screen[i]; }
    } else valid=false;
    if (valid) HKS_TOP(s)[1]=copy.arguments;
    else { HKS_TOP(s)[1].t=HKS_TNIL;HKS_TOP(s)[1].v.ptr=NULL; }
    HKS_TOP(s)+=2;
    pthread_mutex_lock(&lock);widget=find_widget((void *)element);
    if (widget) widget->in_callback=true;
    pthread_mutex_unlock(&lock);
    bool ok=hks_pcall(s,1,0);HKS_TOP(s)=HKS_BASE(s)+top;
    pthread_mutex_lock(&lock);widget=find_widget((void *)element);
    if (widget && widget->callback_ref==copy.callback_ref && widget->vm==s) {
        widget->in_callback=false;if (!ok) widget->failed=true;
    }
    pthread_mutex_unlock(&lock);
}
static int setup(lua_State *s,bool line) {
    if (!atomic_load(&active) || !s) return 0;
    void *element=element_arg(s,1);if (!element) return 0;
    const char *name="tag_weapon_right";
    if (HKS_BASE(s)+1<HKS_TOP(s) && (HKS_BASE(s)[1].t&15)==HKS_TSTRING) name=hks_check_string(s,2);
    if (!name || !*name) name="tag_weapon_right";
    size_t length=0;while (length<sizeof(tags[0].name) && name[length]) ++length;
    if (length==sizeof(tags[0].name)) return 0;
    HksObject callback={0},arguments={0};int callback_ref=-2,table_ref=-2;
    if (line) {
        if (HKS_BASE(s)+5>=HKS_TOP(s)) return 0;
        callback=HKS_BASE(s)[5];unsigned type=callback.t&15;
        if (type!=6 && type!=9 && type!=10) return 0;
        *HKS_TOP(s)++=callback;callback_ref=reference(s,REGISTRY);
        if (callback_ref<0) return 0;
        create_table(s,2,0);arguments=HKS_TOP(s)[-1];table_ref=reference(s,REGISTRY);
        if (table_ref<0 || (arguments.t&15)!=5) {
            unreference(s,REGISTRY,callback_ref);if (table_ref>=0) unreference(s,REGISTRY,table_ref);return 0;
        }
    }
    float offset[3]={0};
    for (unsigned i=0;i<3;++i) {
        float value;if (hks_number_arg(s,(int)i+3,&value) && isfinite(value) && fabsf(value)<=1000000) offset[i]=value;
    }
    unsigned id=0;
    pthread_mutex_lock(&lock);
    for (unsigned i=0;i<tag_count;++i) if (!strcmp(tags[i].name,name)) { id=tags[i].id;break; }
    bool full=!id && tag_count==TAGS;
    pthread_mutex_unlock(&lock);
    if (full) goto fail;
    if (!id) id=intern_string(name,0,(unsigned)length+1,3);
    if (!id) goto fail;
    pthread_mutex_lock(&lock);
    unsigned tag=0;for (;tag<tag_count && tags[tag].id!=id;++tag) {}
    if (tag==tag_count) {
        if (tag_count==TAGS) { pthread_mutex_unlock(&lock);goto fail; }
        /* Match the native process-lifetime tag registry. IDs have a user-0
         * reference and survive level-owned string cleanup. */
        memcpy(tags[tag].name,name,length+1);tags[tag].id=id;++tag_count;
    }
    Widget *widget=find_widget(element);
    if (!widget) {
        if (widget_count==WIDGETS) { pthread_mutex_unlock(&lock);goto fail; }
        widget=&widgets[widget_count++];memset(widget,0,sizeof(*widget));
        widget->callback_ref=widget->table_ref=-2;
    }
    Widget previous=*widget;Layout old=pointer((uint8_t *)element+0x110);
    if (old==layout_line) old=previous.previous;
    *widget=(Widget){.element=element,.tag=tag,.line=line,.vm=s,.global=HKS_GLOBAL(s),
        .callback_ref=callback_ref,.table_ref=table_ref,.callback=callback,.arguments=arguments,.previous=old};
    memcpy(widget->offset,offset,sizeof(offset));
    Layout layout=line ? layout_line : layout_basic;memcpy((uint8_t *)element+0x110,&layout,sizeof(layout));
    pthread_mutex_unlock(&lock);
    if (previous.vm==s && previous.global==HKS_GLOBAL(s)) {
        if (previous.callback_ref>=0) unreference(s,REGISTRY,previous.callback_ref);
        if (previous.table_ref>=0) unreference(s,REGISTRY,previous.table_ref);
    }
    return 0;
fail:
    if (callback_ref>=0) unreference(s,REGISTRY,callback_ref);
    if (table_ref>=0) unreference(s,REGISTRY,table_ref);
    return 0;
}
static int setup_basic(lua_State *s) { return setup(s,false); }
static int setup_line(lua_State *s) { return setup(s,true); }
static Method line_method={"setupViewmodelTagWidgetLine",setup_line,NULL};
static Method basic_method={"setupViewmodelTagWidget",setup_basic,&line_method};
static void *get_matrices(void *object) {
    void *result=original_matrices(object);uintptr_t value=(uintptr_t)object;
    if (!atomic_load(&active) || (value!=atomic_load(&objects[0]) && value!=atomic_load(&objects[1]))) return result;
    pthread_mutex_lock(&lock);
    for (unsigned client=0;client<CLIENTS;++client) {
        if (value!=atomic_load(&objects[client]) || models[client]!=pointer((uint8_t *)object+0x18)) continue;
        unsigned count=read16((uint8_t *)object+0x120);
        for (unsigned i=0;i<tag_count;++i) {
            uint16_t bone=0xfffe;
            if (!result || !count || count>384 || !bone_index(object,tags[i].id,&bone,UINT32_MAX) || bone>=count) {
                tags[i].sample[client].valid=false;continue;
            }
            if (!(read32((uint8_t *)object+0xc8+4*(bone>>5))&(UINT32_C(0x80000000)>>(bone&31)))) continue;
            const uint8_t *matrix=(const uint8_t *)result+(size_t)bone*32;Sample sample={.valid=true};
            memcpy(sample.quaternion,matrix,16);memcpy(sample.position,matrix+16,12);
            float norm=0;for (unsigned j=0;j<4;++j) { norm+=sample.quaternion[j]*sample.quaternion[j];sample.valid&=isfinite(sample.quaternion[j]); }
            sample.valid&=norm>0.25f && norm<4;
            for (unsigned j=0;j<3;++j) sample.valid&=bounded(sample.position[j],10000000);
            tags[i].sample[client]=sample;
        }
    }
    pthread_mutex_unlock(&lock);return result;
}
void t7o_mac_ui_inline_dispatch(T7oInlineContext *context,unsigned operation) {
    if (!atomic_load(&active) || operation!=T7O_INLINE_VIEWMODEL_OBJECT) return;
    const uint8_t *cg=(const void *)(uintptr_t)context->r13;
    unsigned client=read32(cg+4);if (client>=CLIENTS) return;
    void *object=pointer((const void *)(uintptr_t)context->r15);
    void *model=object ? pointer((uint8_t *)object+0x18) : NULL;
    pthread_mutex_lock(&lock);
    if (atomic_load(&objects[client])!=(uintptr_t)object || models[client]!=model) {
        for (unsigned i=0;i<tag_count;++i) tags[i].sample[client].valid=false;
        models[client]=model;atomic_store(&objects[client],(uintptr_t)object);
    }
    pthread_mutex_unlock(&lock);
}
void t7o_mac_ui_deactivate(void) {
    atomic_store(&active,false);
    if (installed) prepatch_set_inline_callback(T7O_MAC_VIEWMODEL_OBJECT,NULL);
    pthread_mutex_lock(&lock);
    for (unsigned client=0;client<CLIENTS;++client) {
        atomic_store(&objects[client],0);models[client]=NULL;
        for (unsigned i=0;i<tag_count;++i) tags[i].sample[client].valid=false;
    }
    /* The engine owns VM destruction. Do not touch old registry handles here;
     * their VM may already be gone. Reconfiguration within the current active
     * generation releases its own replaced handles in setup(). */
    pthread_mutex_unlock(&lock);
}
bool t7o_mac_ui_activate(lua_State *s) {
    if (!installed || !s) return false;
    t7o_mac_ui_deactivate();
    /* Pointer equality cannot prove an old VM survived a reload: allocators
     * can reuse both addresses. Let the old VM reclaim its roots on shutdown.
     * A reused VM retains at most two roots per registered line widget per
     * activation; replacing a widget in this generation releases old roots. */
    memset(widgets,0,sizeof(widgets));widget_count=0;
    Method *generic=address(T7O_MAC_GENERIC_WIDGET_METHOD);
    if (!methods_registered) {
        if (!generic->name || strcmp(generic->name,"setupGeneric3DWidget") ||
            generic->function!=(lua_CFunction)address(T7O_MAC_SETUP_GENERIC_WIDGET)) return false;
        Method *node=generic->next;
        for (unsigned i=0;node && i<2048;++i,node=node->next) {
            if (node==&basic_method) { methods_registered=true;break; }
            if (node==&line_method || (node->name && (!strcmp(node->name,basic_method.name) || !strcmp(node->name,line_method.name)))) return false;
        }
        if (node && !methods_registered) return false;
        if (!methods_registered) { line_method.next=generic->next;generic->next=&basic_method;methods_registered=true; }
    }
    if (!prepatch_set_inline_callback(T7O_MAC_VIEWMODEL_OBJECT,t7o_inline_viewmodel_object)) return false;
    atomic_store(&active,true);return true;
}
bool t7o_mac_ui_install(uint64_t game_base,bool exact_build_verified) {
    if (!T7O_DEV_NATIVE || !exact_build_verified) return false;
    if (installed) return base==game_base;
    base=game_base;
    const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x47,0x58};
    if (memcmp(prepatch_original_code(address(T7O_MAC_BONE_MATRICES),sizeof(prefix)),prefix,sizeof(prefix)) ||
        !prepatch_native_site_present(T7O_MAC_VIEWMODEL_OBJECT)) return false;
    bone_index=address(T7O_MAC_BONE_INDEX);intern_string=address(T7O_MAC_INTERN_STRING_SIZE);
    element_arg=address(T7O_MAC_UI_ELEMENT_ARG);generic_layout=address(T7O_MAC_GENERIC_WIDGET_LAYOUT);
    create_table=address(T7O_MAC_HKS_CREATE_TABLE);reference=address(T7O_MAC_HKS_REF);unreference=address(T7O_MAC_HKS_UNREF);
    project=address(T7O_MAC_PROJECT_WIDGET);
    if (DobbyHook(address(T7O_MAC_BONE_MATRICES),(void *)get_matrices,(void **)&original_matrices)!=0) return false;
    installed=true;return true;
}
