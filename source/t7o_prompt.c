#include "t7o_prompt.h"
#include <math.h>
#include <string.h>
static float clamp(float v,float lo,float hi) { return v<=hi ? (lo<=v ? v:lo):hi; }
void t7o_prompt_reset(T7oPromptState *state) {
    if (state) { memset(state,0,sizeof(*state));state->pending=1; }
}
static float ease(float value) {
    float x=clamp(value,0,1),result;
    if (x>0.55000001192092896f) {
        x=clamp((x-0.55000001192092896f)/0.44999998807907104f,0,1);
        float a=(1-x)*3;
        result=(((a*(1-x)*x)*0.60000002384185791f+a*x*x)+x*x*x)*0.25f+0.75f;
    } else {
        x=clamp(x/0.55000001192092896f,0,1);
        float a=(1-x)*3;
        result=(((a*(1-x)*x)*0.5f+(a*x*x)*0.75555562973022461f)+x*x*x)*0.75f;
    }
    return result;
}
void t7o_prompt_step(T7oPromptState *s,const float b[4],const float current[2],
    float out[2],float transition,unsigned delta,float width) {
    if (!s || !b || !current || !out) return;
    if (!isfinite(transition) || transition<=0 || transition>10000) { memset(s,0,sizeof(*s));return; }
    if (!isfinite(width) || width<320 || width>8192) width=1280;
    float lo[2]={0,0},hi[2]={width,720};bool bounds=true;
    for (unsigned i=0;i<4;++i) bounds&=isfinite(b[i]) && fabsf(b[i])<=32768;
    if (bounds && b[0]<=b[2] && b[1]<=b[3] && b[2]-b[0]<=width && b[3]-b[1]<=720) {
        lo[0]=-b[0];lo[1]=-b[1];hi[0]=width-b[2];hi[1]=720-b[3];
    }
    bool target=isfinite(out[0]) && isfinite(out[1]) && fabsf(out[0])<100000 && fabsf(out[1])<100000;
    float point[2]={0,0};
    if (target) for (unsigned i=0;i<2;++i) point[i]=clamp(out[i],lo[i],hi[i]);
    if (s->pending) {
        s->pending=0;float initial[2]={current[0]-b[0],current[1]-b[1]};
        if (!isfinite(initial[0]) || !isfinite(initial[1])) { memset(s,0,sizeof(*s));return; }
        s->elapsed=0;s->valid=1;
        for (unsigned i=0;i<2;++i) s->from[i]=s->current[i]=clamp(initial[i],lo[i],hi[i]);
        float distance=target ? hypotf(point[0]-s->from[0],point[1]-s->from[1]):320;
        s->duration=clamp((distance*transition)/320,16,10000);
    }
    if (!s->valid) return;
    float dt=clamp((float)delta,1,100);s->elapsed=fminf(s->duration,s->elapsed+dt);
    if (!(s->elapsed<s->duration)) { memset(s,0,sizeof(*s));return; }
    if (target) { memcpy(s->target,point,sizeof(point));s->have_target=1; }
    const float *destination=s->have_target ? s->target:s->current;
    float fraction=ease(s->elapsed/s->duration),change[2];
    for (unsigned i=0;i<2;++i) change[i]=((destination[i]-s->from[i])*fraction+s->from[i])-s->current[i];
    float distance=hypotf(change[0],change[1]),speed=dt*4,scale=distance>speed ? speed/distance:1;
    for (unsigned i=0;i<2;++i) out[i]=s->current[i]=clamp(scale*change[i]+s->current[i],lo[i],hi[i]);
}
