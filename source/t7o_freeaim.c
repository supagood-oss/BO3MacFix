#include "t7o_freeaim.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
static bool space(unsigned c) { return c==' ' || (c>=9 && c<=13); }
static bool digit(unsigned c) { return c>='0' && c<='9'; }
bool t7o_freeaim_number(const char **cursor,float *out) {
    const unsigned char *p=(const unsigned char *)*cursor;double value=0,weight=1;int sign=1;
    while (space(*p)) ++p;
    if (*p=='+' || *p=='-') { if (*p=='-') sign=-1;++p; }
    bool any=false;
    while (digit(*p)) { any=true;value=value*10+(*p++-'0'); }
    if (*p=='.') {
        ++p;
        while (digit(*p)) { any=true;weight*=0.1;value+=(*p++-'0')*weight; }
    }
    if (!any) return false;
    if (*p=='e' || *p=='E') {
        const unsigned char *exponent=p+1;int exponent_sign=1,power=0;
        if (*exponent=='+' || *exponent=='-') { if (*exponent=='-') exponent_sign=-1;++exponent; }
        if (digit(*exponent)) {
            do { if (power<100) power=power*10+(*exponent-'0');++exponent; } while (digit(*exponent));
            power*=exponent_sign;if (power < -38 || power>38) return false;
            for (;power>0;--power) value*=10;
            for (;power<0;++power) value*=0.1;
            p=exponent;
        }
    }
    value*=sign;
    if (!isfinite(value) || fabs(value)>16777216.0) return false;
    *cursor=(const char *)p;*out=(float)value;return true;
}
bool t7o_freeaim_target(const char *text,float out[3]) {
    if (!text) return false;
    size_t n=0;while (n<192 && text[n]) ++n;if (n==192) return false;
    float result[3];const char *p=text;
    for (unsigned i=0;i<3;++i) {
        if (!t7o_freeaim_number(&p,&result[i]) || (i<2 && !space((unsigned char)*p))) return false;
    }
    while (space((unsigned char)*p)) ++p;
    if (*p) return false;
    memcpy(out,result,sizeof(result));return true;
}
T7oAimCommand t7o_freeaim_command(const char *text,float out[3]) {
    if (!text) return T7O_AIM_INVALID;
    size_t n=0;while (n<128 && text[n]) ++n;
    if (n==128 || n<4 || memcmp(text,"aae ",4)) return T7O_AIM_INVALID;
    if (!strcmp(text+4,"off")) return T7O_AIM_OFF;
    const unsigned char *p=(const unsigned char *)text+4;float result[3];
    for (unsigned i=0;i<3;++i) {
        while (space(*p)) ++p;
        int sign=1;if (*p=='+' || *p=='-') { if (*p=='-') sign=-1;++p; }
        if (!digit(*p)) return T7O_AIM_INVALID;
        uint32_t value=0;
        do {
            unsigned next=*p++-'0';
            if (value>838860 || (value==838860 && next>8)) return T7O_AIM_INVALID;
            value=value*10+next;
        } while (digit(*p));
        if (i<2 && !space(*p)) return T7O_AIM_INVALID;
        result[i]=(float)((int)value*sign)*2;
    }
    while (space(*p)) ++p;
    if (*p) return T7O_AIM_INVALID;
    memcpy(out,result,sizeof(result));return T7O_AIM_TARGET;
}
static bool normalize(float v[3]) {
    float length=sqrtf(v[1]*v[1]+v[0]*v[0]+v[2]*v[2]);
    if (!isfinite(length) || length<0.000001f) return false;
    float inverse=1/length;for (unsigned i=0;i<3;++i) v[i]*=inverse;return true;
}
bool t7o_freeaim_rotate(const float origin[3],const float axis[9],const float target[3],float out[9]) {
    for (unsigned i=0;i<3;++i) {
        if (!isfinite(origin[i]) || fabsf(origin[i])>16777216.0f) return false;
        const float *row=axis+i*3;float length=row[1]*row[1]+row[0]*row[0]+row[2]*row[2];
        if (!isfinite(length) || fabsf(length-1)>0.05f) return false;
    }
    for (unsigned a=0;a<2;++a) for (unsigned b=a+1;b<3;++b) {
        const float *x=axis+a*3,*y=axis+b*3;float dot=x[1]*y[1]+x[0]*y[0]+x[2]*y[2];
        if (fabsf(dot)>0.05f) return false;
    }
    float from[3]={axis[0],axis[1],axis[2]},to[3];
    for (unsigned i=0;i<3;++i) to[i]=target[i]-origin[i];
    if (!normalize(from) || !normalize(to)) return false;
    float x=from[1]*to[2]-to[1]*from[2],y=to[0]*from[2]-from[0]*to[2],z=from[0]*to[1]-to[0]*from[1];
    float w=from[0]*to[0]+to[1]*from[1]+from[2]*to[2]+1;
    float length=sqrtf(x*x+y*y+z*z+w*w);
    if (!isfinite(length)) return false;
    if (length<0.000001f) {
        float seed_x=fabsf(from[2])<=0.9f ? 0:1,seed_z=fabsf(from[2])<=0.9f ? 1:0;
        x=from[1]*seed_z-from[2]*0;
        z=from[0]*0-from[1]*seed_x;
        y=from[2]*seed_x-from[0]*seed_z;w=0;
        length=sqrtf(y*y+x*x+z*z);if (length<0.000001f) return false;
    }
    float inverse=1/length;x*=inverse;y*=inverse;z*=inverse;w*=inverse;
    float result[9];
    for (unsigned row=0;row<3;++row) {
        const float *p=axis+row*3;float a=p[2]*y-p[1]*z,b=p[0]*z-p[2]*x,c=p[1]*x-p[0]*y;
        a+=a;b+=b;c+=c;
        result[row*3]=(w*a+p[0]+y*c)-z*b;
        result[row*3+1]=(w*b+p[1]+z*a)-x*c;
        result[row*3+2]=(w*c+p[2]+x*b)-y*a;
        for (unsigned i=0;i<3;++i) if (!isfinite(result[row*3+i])) return false;
    }
    memcpy(out,result,sizeof(result));return true;
}
static bool trace_point(const float p[3]) {
    for (unsigned i=0;i<3;++i) if (!isfinite(p[i]) || fabsf(p[i])>=10000000) return false;
    return true;
}
static float trace_distance(const float a[3],const float b[3]) {
    float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return z*z+y*y+x*x+0.0f;
}
bool t7o_freeaim_trace_select(T7oAimSnapshot records[32],unsigned cursor,uint64_t now,
    const float origin[3],const float end[3],const float muzzle[3],float start_out[3],float end_out[3]) {
    if (!records || !origin || !end || !muzzle || !start_out || !end_out ||
        !trace_point(origin) || !trace_point(end) || !trace_point(muzzle)) return false;
    for (unsigned i=0;i<32;++i) {
        T7oAimSnapshot *record=&records[(cursor-i-1)&31];
        if (!record->time || record->consumed || record->time>now || now-record->time>500 ||
            !(trace_distance(origin,record->origin)<=64) || !trace_point(record->muzzle) || !trace_point(record->target) ||
            !(trace_distance(muzzle,record->muzzle)<=16384) || !(trace_distance(origin,muzzle)<=16384) ||
            !(trace_distance(origin,record->muzzle)<=16384)) continue;
        double x=(double)record->target[0]-record->muzzle[0],y=(double)record->target[1]-record->muzzle[1],
            z=(double)record->target[2]-record->muzzle[2];double distance=z*z+y*y+x*x+0.0;
        if (!(distance>=1 && distance<=17179869184.0)) continue;
        record->consumed=1;memcpy(start_out,record->muzzle,12);memcpy(end_out,record->target,12);return true;
    }
    return false;
}
