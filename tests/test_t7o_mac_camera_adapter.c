#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define T7O_DEV_NATIVE 1
static void *test_address(uint32_t);
#define T7O_MAC_ADDRESS(offset) test_address(offset)
#include "../source/t7o_mac_camera.c"
static uint8_t cg_data[CLIENTS*T7O_MAC_CG_STRIDE],files[0x1000e0],poses[6][0x408];
static uintptr_t cg_pointer=(uintptr_t)cg_data;static uint32_t cg_count=2;
static Builtin table[3];static unsigned draws,refreshes,axis_calls,method_calls,saves,loads;
static void *expected_object,*last_shader;static unsigned last_client,last_entity;
static bool delete_expected;
static void *test_address(uint32_t rva) {
    if (rva==T7O_MAC_CG_ARRAY) return &cg_pointer;
    if (rva==T7O_MAC_CG_COUNT) return &cg_count;
    if (rva==T7O_MAC_CHECKPOINT_FILES) return files;
    for (unsigned i=0;i<3;++i) if (rva==rows[i]) return &table[i];
    if (rva==T7O_MAC_CSC_DELETE || rva==T7O_MAC_CSC_FORCE_DELETE || rva==T7O_MAC_CSC_CAMERA) return (void *)originals[0];
    abort();
}
const void *prepatch_original_code(const void *p,size_t n) { (void)p;(void)n;abort(); }
int BO3MacFix_Hook(void *p,void *f,void **o) { (void)p;(void)f;(void)o;abort(); }
void t7o_mac_checkpoint_client_load_begin(void *file) { (void)file; }
void t7o_mac_checkpoint_client_load_end(void *file) { (void)file; }
static void axis_mock(const float *angles,float *axis) {
    ++axis_calls;memset(axis,0,36);axis[0]=angles[0]+1;axis[4]=angles[1]+1;axis[8]=angles[2]+1;
}
static void draw_mock(void *object,void *pose,unsigned entity,unsigned flags,const void *shader) {
    assert(object==expected_object && pose && flags==17);++draws;last_entity=entity;last_shader=(void *)shader;
    if (shader && (*(const uint8_t *)shader&8)) {
        uintptr_t descriptor=pointer((const uint8_t *)shader+24);assert(*(const uint8_t *)descriptor==91);
    }
}
static void refresh_mock(int client,int entity) { ++refreshes;last_client=(unsigned)client;last_entity=(unsigned)entity; }
static void method_mock(unsigned i,uint64_t low,uint64_t high) {
    assert(i==1 && (low==12 || low==13) && high==0);++method_calls;
    if (delete_expected) assert(u32(cg_data+LINK_OFFSET)==1);
}
static void save_mock(void *file) { assert(save_slot(file)>=0);++saves; }
static void load_mock(void *file) {
    assert(save_slot(file)>=0);++loads;
    assert(!u32(cg_data+LINK_OFFSET));
    memset(cg_data+LINK_OFFSET,0x7f,sizeof(Links)); /* original load may overwrite link fields */
}
static void put_float(void *p,float v) { memcpy(p,&v,4); }
static float get_float(const void *p) { float v;memcpy(&v,p,4);return v; }
static void links_init(unsigned client) {
    uint8_t *cg=cg_for(client);Links links={.count=2,.entity={12,13},.mode={3,5},.offset={{2,3,4},{0,0,0}}};
    memcpy(cg+LINK_OFFSET,&links,sizeof(links));put32(cg+T7O_MAC_CG_TIME,100);
    for (unsigned i=0;i<3;++i) { put_float(cg+VIEW_ORIGIN+i*4,10+(float)i);put_float(cg+VIEW_ANGLES+i*4,20+(float)i); }
    float axis[9]={1,0,0,0,1,0,0,0,1};memcpy(cg+VIEW_AXIS,axis,36);eligible(&cameras[client],cg);
}
static void flush(unsigned client) {
    uint8_t *cg=cg_for(client);angles_to_axis((float *)(cg+VIEW_ANGLES),(float *)(cg+VIEW_AXIS));
}
int main(void) {
    assert(!t7o_mac_camera_install(0,false) && !t7o_mac_camera_activate());
    original_draw=draw_mock;original_axis=axis_mock;refresh_pose=refresh_mock;original_save=save_mock;original_load=load_mock;
    for (unsigned i=0;i<3;++i) originals[i]=method_mock;
    installed=true;expected_object=(void *)0x1234;
    const char *names[]={"delete","forcedelete","linktocamera"};
    for (unsigned i=0;i<3;++i) table[i]=(Builtin){.hash=t7o_script_hash(names[i]),.min_args=i==2,.max_args=i==2 ? 2:0,.function=method_mock};
    ++table[2].hash;assert(!t7o_mac_camera_activate() && table[0].function==method_mock);--table[2].hash;
    assert(t7o_mac_camera_activate() && table[2].min_args==0 && table[2].max_args==2);
    links_init(0);links_init(1);
    for (unsigned i=0;i<6;++i) { poses[i][2]=i==5;put32(poses[i]+POSE_ENTITY,i==5 ? 12:12+i); }
    uint8_t shader[32]={8},descriptor[48]={91};uintptr_t d=(uintptr_t)descriptor;memcpy(shader+24,&d,8);
    draw(expected_object,poses[0],12,17,shader);assert(!draws && cameras[0].pending_count==1);
    memset(shader,0,sizeof(shader));memset(descriptor,0,sizeof(descriptor)); /* queued copies survive stack reuse */
    flush(0);assert(draws==1 && refreshes==1 && last_client==0 && last_entity==12 && last_shader);
    /* The supplied refdef axis is what the original angle calculation produced. */
    assert(get_float(poses[0]+0x40)==52 && get_float(poses[0]+0x44)==77 && get_float(poses[0]+0x48)==104);
    assert(get_float(poses[0]+0x4c)==20 && get_float(poses[0]+0x54)==22);
    draw(expected_object,poses[0],12,17,NULL);flush(0);assert(draws==2 && refreshes==1 && !last_shader);
    draw(expected_object,poses[0],12,17,NULL);put32(cg_data+T7O_MAC_CG_TIME,101);flush(0);assert(draws==2);
    draw(expected_object,poses[0],12,17,NULL);put32(poses[0]+POSE_ENTITY,99);flush(0);assert(draws==2);put32(poses[0]+POSE_ENTITY,12);
    draw(expected_object,poses[5],12,17,NULL);flush(0);assert(draws==2);flush(1);assert(draws==3 && last_client==1);
    /* An unrelated axis conversion does not drain the queue. */
    draw(expected_object,poses[0],12,17,NULL);float angles[3]={0},axis[9];angles_to_axis(angles,axis);assert(cameras[0].pending_count==1);
    delete_expected=true;delete_entity(1,12,0);delete_expected=false;assert(method_calls==1 && !cameras[0].pending_count && u32(cg_data+LINK_OFFSET+4)==13);
    links_init(0);link_to_camera(1,12,0);assert(method_calls==2 && cameras[0].count==2);
    void *slot0=files+0x80070,*slot1=files+0xc00a8;uintptr_t buffer=0xabc000;memcpy(slot0,&buffer,8);buffer+=4096;memcpy(slot1,&buffer,8);
    save_client(slot0);remove_entity(0,12);save_client(slot1);
    uint8_t disk_camera[176];t7o_mac_camera_checkpoint_export(0,pointer(slot0),disk_camera);
    assert(t7o_mac_camera_checkpoint_valid(disk_camera));
    memset(saved,0,sizeof(saved));assert(t7o_mac_camera_checkpoint_import(0,pointer(slot0),disk_camera));
    load_client(slot0);assert(u32(cg_data+LINK_OFFSET)==2);
    disk_camera[4]=4;assert(!t7o_mac_camera_checkpoint_import(1,pointer(slot1),disk_camera));disk_camera[4]=3;
    put32(disk_camera+8,5);assert(!t7o_mac_camera_checkpoint_valid(disk_camera));
    save_client(slot1);
    load_client(slot0);assert(u32(cg_data+LINK_OFFSET)==2 && cameras[0].count==2);
    load_client(slot1);assert(u32(cg_data+LINK_OFFSET)==2 && u32(cg_data+LINK_OFFSET+4)==12);
    buffer+=4096;memcpy(slot0,&buffer,8);load_client(slot0);assert(!u32(cg_data+LINK_OFFSET));
    assert(saves==3 && loads==4 && !loading);
    t7o_mac_camera_deactivate();assert(table[2].min_args==1 && table[2].function==method_mock && !saved[0].valid);
    draw(expected_object,poses[0],12,17,NULL);assert(draws==4);
    puts("Mac camera queue, shader lifetime, pose refresh, split-screen, deletion, rotating checkpoint slots and unload tests passed (substituted engine)");
}
