#include "t7o_movement.h"
#include <math.h>
#include <string.h>
#ifdef __clang__
#pragma clang fp contract(off)
#endif
void t7o_slide_momentum(float velocity[3],uint64_t flags,const T7oMovementLocal *local,
    T7oAccelerate accelerate,void *context) {
    if (!velocity || !local || !accelerate) return;
    bool primary=(local->flags&2)!=0;
    if (!primary && (!(flags&UINT64_C(0x8000000000000)) || !local->contact)) return;
    float before[3];memcpy(before,velocity,sizeof(before));
    float x=local->direction[0],y=local->direction[1];
    float root=sqrtf(x*x+y*y+0.0f);
    if (root<=0.0f) root=1.0f;
    float inverse=(float)(1.0/(double)root);
    float nx=x*inverse,ny=y*inverse;
    float direction[3]={nx,ny,(float)((double)inverse*0.0)};
    if (primary) { accelerate(context,direction,300.0f,2.0f);return; }
    float double_x=(float)((double)nx*2.0),double_y=(float)((double)ny*2.0);
    float old_x=(float)((double)before[0]*0.0066666668);
    float old_y=(float)((double)before[1]*0.0066666668);
    direction[0]=(float)((double)(double_x+old_x)*0.333);
    direction[1]=(float)((double)(double_y+old_y)*0.333);
    float z=(float)((double)direction[2]*2.0);
    direction[2]=(float)(((double)z+0.0)*0.333);
    accelerate(context,direction,1000.0f,0.25f);
    float speed=sqrtf(velocity[0]*velocity[0]+velocity[1]*velocity[1]+velocity[2]*velocity[2]);
    if (speed>90000.0f) memcpy(velocity,before,sizeof(before));
}
