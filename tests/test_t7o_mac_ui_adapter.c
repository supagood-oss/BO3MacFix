#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static void *test_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#define T7O_DEV_NATIVE 1
#include "../source/t7o_mac_ui.c"
static uint8_t state_bytes[0x80],global_bytes[0x700],other_global[0x700],object_bytes[0x130],element[0x1a0],cg[32];
static HksObject stack_values[256],references[128];
static uint8_t table_bytes[16][64];static HksObject table_arrays[16][2];
static float matrices[2][8];static void *cg_array=cg;static uint32_t cg_count=1;
static unsigned ref_count,unrefs,created,interns,layouts,callbacks,nil_callbacks;
static bool fail_callback,found_bone=true,reenter;
static Method tail={"existingMethod",NULL,NULL},method={"setupGeneric3DWidget",NULL,&tail};
static lua_State *vm(void) { return (lua_State *)state_bytes; }
static int generic_setup_mock(lua_State *s) { (void)s;return 0; }
static void *test_address(uint32_t rva) {
    if (rva==T7O_MAC_GENERIC_WIDGET_METHOD) return &method;
    if (rva==T7O_MAC_SETUP_GENERIC_WIDGET) return (void *)generic_setup_mock;
    if (rva==T7O_MAC_CG_COUNT) return &cg_count;
    if (rva==T7O_MAC_CG_ARRAY) return &cg_array;
    abort();
}
static void *get_element(lua_State *s,int index) { assert(index==1);return HKS_BASE(s)[0].v.ptr; }
const char *hks_check_string(lua_State *s,int index) { return HKS_BASE(s)[index-1].v.ptr; }
bool hks_number_arg(lua_State *s,int index,float *out) {
    HksObject *value=HKS_BASE(s)+index-1;
    if (value>=HKS_TOP(s) || (value->t&15)!=HKS_TNUMBER) return false;
    *out=value->v.number;return true;
}
static unsigned intern_mock(const char *name,unsigned user,unsigned length,unsigned type) {
    assert(!strcmp(name,"tag_weapon_right") && !user && length==strlen(name)+1 && type==3);++interns;return 77;
}
static int bone_mock(void *object,unsigned id,uint16_t *bone,unsigned mask) {
    assert(object==object_bytes && id==77 && *bone==0xfffe && mask==UINT32_MAX);*bone=1;return found_bone;
}
static void *matrices_mock(void *object) { assert(object==object_bytes);return matrices; }
static void table_mock(lua_State *s,int array,int hash) {
    assert(array==2 && !hash && created<16);void *p=table_arrays[created];unsigned size=2;
    memcpy(table_bytes[created]+0x28,&p,sizeof(p));memcpy(table_bytes[created]+0x30,&size,4);
    HKS_TOP(s)->t=5;HKS_TOP(s)->v.ptr=table_bytes[created++];++HKS_TOP(s);
}
static int ref_mock(lua_State *s,int index) {
    assert(index==REGISTRY && ref_count+1<128);references[++ref_count]=*--HKS_TOP(s);return (int)ref_count;
}
static void unref_mock(lua_State *s,int index,int ref) {
    assert(s==vm() && HKS_GLOBAL(s)==global_bytes && index==REGISTRY && ref>0 && (unsigned)ref<=ref_count);
    assert((references[ref].t&15)!=0);references[ref].t=0;++unrefs;
}
static void generic_mock(uintptr_t client,uintptr_t value,uintptr_t root,uintptr_t a,uintptr_t b,uintptr_t s) {
    assert(!client && value==(uintptr_t)element && root==0xaa && a==0xbb && b==0xcc && s==(uintptr_t)vm());++layouts;
}
static void project_mock(int client,const float *world,float *screen) {
    assert(!client);screen[0]=world[0]*10;screen[1]=world[1]*20;
}
bool hks_pcall(lua_State *s,int nargs,int results) {
    assert(s==vm() && nargs==1 && !results && (HKS_TOP(s)[-2].t&15)==6);++callbacks;
    HksObject argument=HKS_TOP(s)[-1];
    if ((argument.t&15)==0) ++nil_callbacks;
    else {
        assert((argument.t&15)==5);HksObject *array=pointer((uint8_t *)argument.v.ptr+0x28);
        assert(array[0].t==3 && array[1].t==3 && isfinite(array[0].v.number) && isfinite(array[1].v.number));
    }
    if (reenter) { reenter=false;layout_line(0,(uintptr_t)element,0xaa,0xbb,0xcc,(uintptr_t)vm()); }
    return !fail_callback;
}
bool prepatch_set_inline_callback(uint32_t site,void (*fn)(void)) {
    assert(site==T7O_MAC_VIEWMODEL_OBJECT && (!fn || fn==t7o_inline_viewmodel_object));return true;
}
bool prepatch_native_site_present(uint32_t site) { (void)site;abort(); }
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *replacement,void **original) { (void)p;(void)replacement;(void)original;abort(); }
void t7o_inline_viewmodel_object(void) {}
static void put32(void *p,uint32_t value) { memcpy(p,&value,4); }
static void arguments(void) {
    HKS_BASE(vm())=stack_values;HKS_TOP(vm())=stack_values+6;
    stack_values[0]=(HksObject){.t=7,.v.ptr=element};
    stack_values[1]=(HksObject){.t=4,.v.ptr="tag_weapon_right"};
    for (unsigned i=0;i<3;++i) stack_values[i+2]=(HksObject){.t=3,.v.number=(float)i+1};
    stack_values[5]=(HksObject){.t=6,.v.ptr=(void *)0x1234};
}
static void layout(void) {
    Layout fn=pointer(element+0x110);assert(fn);fn(0,(uintptr_t)element,0xaa,0xbb,0xcc,(uintptr_t)vm());
}
int main(void) {
    HKS_GLOBAL(vm())=global_bytes;arguments();method.function=generic_setup_mock;
    element_arg=get_element;intern_string=intern_mock;bone_index=bone_mock;original_matrices=matrices_mock;
    create_table=table_mock;reference=ref_mock;unreference=unref_mock;generic_layout=generic_mock;project=project_mock;
    base=0x100000000;
    assert(!t7o_mac_ui_install(base,false) && !t7o_mac_ui_activate(vm()));
    installed=true;tail.next=&tail;assert(!t7o_mac_ui_activate(vm()) && method.next==&tail);
    tail.next=NULL;assert(t7o_mac_ui_activate(vm()));
    assert(method.next==&basic_method && basic_method.next==&line_method && line_method.next==&tail);
    assert(!setup_basic(vm()) && tag_count==1 && widget_count==1 && interns==1 && HKS_TOP(vm())==stack_values+6);
    layout();assert(!layouts); /* Invalid snapshots must not project an uninitialized position. */
    void *object=object_bytes,*model=(void *)0x10,*matrix=matrices;
    memcpy(object_bytes+0x18,&model,sizeof(model));memcpy(object_bytes+0x58,&matrix,sizeof(matrix));
    uint16_t count=2;memcpy(object_bytes+0x120,&count,2);put32(object_bytes+0xc8,0x40000000);
    matrices[1][3]=1;matrices[1][4]=10;matrices[1][5]=20;matrices[1][6]=30;
    T7oInlineContext context={0};context.r13=(uintptr_t)cg;context.r15=(uintptr_t)&object;
    t7o_mac_ui_inline_dispatch(&context,T7O_INLINE_VIEWMODEL_OBJECT);
    assert(get_matrices(object_bytes)==matrices);layout();assert(layouts==1);
    float point[3];memcpy(point,element+0x78,sizeof(point));assert(point[0]==11 && point[1]==22 && point[2]==33);
    assert(!setup_line(vm()) && interns==1 && widget_count==1 && created==1 && ref_count==2);
    layout();assert(callbacks==1 && !nil_callbacks && HKS_TOP(vm())==stack_values+6);
    assert(table_arrays[0][0].v.number==110 && table_arrays[0][1].v.number==440);
    reenter=true;layout();assert(callbacks==2 && !widgets[0].in_callback);
    matrices[1][2]=sqrtf(0.5f);matrices[1][3]=sqrtf(0.5f);get_matrices(object_bytes);
    assert(world_position(0,&widgets[0],point) && fabsf(point[0]-8)<0.0001f && fabsf(point[1]-21)<0.0001f);
    found_bone=false;get_matrices(object_bytes);layout();assert(nil_callbacks==1);found_bone=true;
    get_matrices(object_bytes);cg_count=0;layout();assert(nil_callbacks==2);cg_count=1;
    fail_callback=true;layout();unsigned before=callbacks;layout();assert(callbacks==before && widgets[0].failed);
    fail_callback=false;assert(!setup_line(vm()) && unrefs==2 && !widgets[0].failed);
    before=callbacks;layout();assert(callbacks==before+1);
    tag_count=TAGS;stack_values[1].v.ptr="unregistered_tag";before=unrefs;
    assert(!setup_line(vm()) && unrefs==before+2 && interns==1 && widget_count==1);
    tag_count=1;stack_values[1].v.ptr="tag_weapon_right";
    before=unrefs;assert(t7o_mac_ui_activate(vm()) && unrefs==before && !widget_count && tag_count==1);
    assert(method.next==&basic_method && line_method.next==&tail);
    arguments();assert(!setup_line(vm()));before=unrefs;
    HKS_GLOBAL(vm())=other_global;assert(t7o_mac_ui_activate(vm()) && unrefs==before); /* Never unref an old heap. */
    t7o_mac_ui_deactivate();before=layouts;layout_basic(0,(uintptr_t)element,0xaa,0xbb,0xcc,(uintptr_t)vm());
    assert(layouts==before+1);
    puts("AAE UI: method links, bone/rotation snapshots, Lua roots and tables, nil/error/reentry behavior, heap changes and inactive fallback passed (substituted engine)");
}
