#include "../source/t7o_freeaim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    float point[3]={99,98,97};
    assert(t7o_freeaim_target(" 1.5 -2.25 +3e2 \t",point));
    assert(point[0]==1.5f && point[1]==-2.25f && point[2]==300);
    const char *invalid[]={"1,2,3","nan 0 0","1e100 2 3","1 2 3 4","16777217 0 0","1 2","1e 2 3"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        assert(!t7o_freeaim_target(invalid[i],point));assert(point[0]==1.5f && point[2]==300);
    }
    assert(t7o_freeaim_command("aae off",point)==T7O_AIM_OFF && point[0]==1.5f);
    assert(t7o_freeaim_command("aae 8388608 -8388608 +0",point)==T7O_AIM_TARGET);
    assert(point[0]==16777216 && point[1]==-16777216 && point[2]==0);
    assert(t7o_freeaim_command("aae 8388609 0 0",point)==T7O_AIM_INVALID);
    assert(t7o_freeaim_command("aae 1.5 0 0",point)==T7O_AIM_INVALID);
    assert(t7o_freeaim_command("aae 1 2 3 bad",point)==T7O_AIM_INVALID);
    assert(t7o_freeaim_command("aae off ",point)==T7O_AIM_INVALID);
    const float origin[3]={100,200,300},axis[9]={1,0,0,0,-1,0,0,0,1};
    float target[3]={100,210,300},out[9];
    assert(t7o_freeaim_rotate(origin,axis,target,out));
    assert(fabsf(out[0])<0.000001f && fabsf(out[1]-1)<0.000001f && fabsf(out[3]-1)<0.000001f);
    target[0]=90;target[1]=200;assert(t7o_freeaim_rotate(origin,axis,target,out) && out[0]==-1);
    float unchanged[9];memcpy(unchanged,out,sizeof(out));
    assert(!t7o_freeaim_rotate(origin,axis,origin,out) && !memcmp(out,unchanged,sizeof(out)));
    target[0]=NAN;assert(!t7o_freeaim_rotate(origin,axis,target,out));
    float invalid_axis[9]={1,1,0,0,-1,0,0,0,1};target[0]=101;
    assert(!t7o_freeaim_rotate(origin,invalid_axis,target,out));
    puts("Free aim decimal/vector/command contracts, coordinate limits, rotation, opposite vectors and invalid-input preservation passed");
}
