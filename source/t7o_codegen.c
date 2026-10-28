#include "t7o_codegen.h"
#include <stdbool.h>
#include <limits.h>
#include <string.h>
static bool relative(uint8_t *destination,uint64_t target,uint64_t next) {
    if ((target>=next && target-next>INT32_MAX) ||
        (target<next && next-target>UINT64_C(2147483648))) return false;
    int32_t value=target>=next ? (int32_t)(target-next) : (int32_t)(-(int64_t)(next-target));
    memcpy(destination,&value,4);return true;
}
size_t t7o_codegen_float(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t original[8]) {
    if (!output || !original || capacity<48 || code>UINT64_MAX-48 || site>UINT64_MAX-8 ||
        original[0]!=0xf3 || original[1]!=0x0f ||
        !(original[2]==0x10 || original[2]==0x59 || original[2]==0x5e) || (original[3]&0xc7)!=5) return 0;
    int32_t displacement;memcpy(&displacement,original+4,4);
    if (displacement<0 && site+8<(uint64_t)(-(int64_t)displacement)) return 0;
    if (displacement>=0 && site+8>UINT64_MAX-(uint32_t)displacement) return 0;
    uint64_t target=site+8+displacement;
    uint8_t bytes[48]={0x9c,0x48,0x83,0x3d};
    if (!relative(bytes+4,slot,code+9)) return 0;
    bytes[8]=0;bytes[9]=0x74;bytes[10]=14;bytes[11]=0x9d;
    memcpy(bytes+12,original,4);
    if (!relative(bytes+16,slot,code+20)) return 0;
    bytes[20]=0xe9;
    if (!relative(bytes+21,site+8,code+25)) return 0;
    bytes[25]=0x9d;memcpy(bytes+26,original,8);
    if (!relative(bytes+30,target,code+34)) return 0;
    bytes[34]=0xe9;
    if (!relative(bytes+35,site+8,code+39)) return 0;
    memcpy(output,bytes,39);return 39;
}
size_t t7o_codegen_inline(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t *original,size_t length,int short_jcc) {
    if (!output || !original || length<5 || length>8 || capacity<64 ||
        code>UINT64_MAX-64 || site>UINT64_MAX-length) return 0;
    if (short_jcc && !(original[0]>=0x70 && original[0]<=0x7f)) return 0;
    uint8_t bytes[64]={0x48,0x8d,0xa4,0x24,0x80,0xff,0xff,0xff,0x9c,0x48,0x83,0x3d};
    if (!relative(bytes+12,slot,code+17)) return 0;
    bytes[16]=0;bytes[17]=0x74;bytes[18]=9;bytes[19]=0x9d;bytes[20]=0xff;bytes[21]=0x15;
    if (!relative(bytes+22,slot,code+26)) return 0;
    bytes[26]=0xeb;bytes[27]=1;bytes[28]=0x9d;
    const uint8_t restore[8]={0x48,0x8d,0xa4,0x24,0x80,0,0,0};memcpy(bytes+29,restore,8);
    size_t n=37;
    if (short_jcc) {
        int8_t displacement=(int8_t)original[1];
        if (displacement<0 && site+2<(uint64_t)(-(int64_t)displacement)) return 0;
        if (displacement>=0 && site+2>UINT64_MAX-(uint8_t)displacement) return 0;
        uint64_t target=site+2+displacement;
        if (target>=site && target<site+length) return 0;
        bytes[n++]=0x0f;bytes[n++]=0x80 | (original[0]&15);
        if (!relative(bytes+n,target,code+n+4)) return 0;
        n+=4;memcpy(bytes+n,original+2,length-2);n+=length-2;
    } else { memcpy(bytes+n,original,length);n+=length; }
    bytes[n++]=0xe9;
    if (!relative(bytes+n,site+length,code+n+4)) return 0;
    n+=4;memcpy(output,bytes,n);return n;
}
size_t t7o_codegen_ads_scale(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t original[8]) {
    const uint8_t contract[8]={0xf3,0x0f,0x58,0xc0,0x0f,0x29,0x45,0xc0};
    if (!output || !original || memcmp(original,contract,8) || capacity<48 ||
        code>UINT64_MAX-48 || site>UINT64_MAX-8) return 0;
    uint8_t bytes[48]={0x9c,0x48,0x83,0x3d};
    if (!relative(bytes+4,slot,code+9)) return 0;
    bytes[8]=0;bytes[9]=0x74;bytes[10]=18; /* zero -> original */
    bytes[11]=0x9d;bytes[12]=0xf3;bytes[13]=0x0f;bytes[14]=0x59;bytes[15]=0x05;
    if (!relative(bytes+16,slot,code+20)) return 0;
    memcpy(bytes+20,original+4,4);bytes[24]=0xe9;
    if (!relative(bytes+25,site+8,code+29)) return 0;
    bytes[29]=0x9d;memcpy(bytes+30,original,8);bytes[38]=0xe9;
    if (!relative(bytes+39,site+8,code+43)) return 0;
    memcpy(output,bytes,43);return 43;
}
size_t t7o_codegen_cmp32(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t *original,size_t length) {
    uint8_t reg;
    if (!output || !original || capacity<32 || code>UINT64_MAX-32 || site>UINT64_MAX-length) return 0;
    if (length==5 && original[0]==0x3d) reg=0;
    else if (length==6 && original[0]==0x81 && original[1]>=0xf8) reg=original[1]-0xf8;
    else return 0;
    uint8_t bytes[32]={0x48,0x83,0x3d};
    if (!relative(bytes+3,slot,code+8)) return 0;
    bytes[7]=0;bytes[8]=0x74;bytes[9]=8; /* unset -> original comparison */
    bytes[10]=0x3b;bytes[11]=0x05 | (reg<<3);
    if (!relative(bytes+12,slot,code+16)) return 0;
    bytes[16]=0xeb;bytes[17]=(uint8_t)length;
    memcpy(bytes+18,original,length);
    bytes[18+length]=0xe9;
    if (!relative(bytes+19+length,site+length,code+23+length)) return 0;
    memcpy(output,bytes,23+length);return 23+length;
}
size_t t7o_codegen_branch(uint8_t *output,size_t capacity,uint64_t code,
    uint64_t site,uint64_t slot,const uint8_t *original,size_t length,int force_taken) {
    if (!output || !original || length<5 || length>8 || capacity<48 ||
        code>UINT64_MAX-48 || site>UINT64_MAX-length) return 0;
    size_t branch;uint8_t condition;int32_t displacement;
    if (original[0]>=0x70 && original[0]<=0x7f) {
        branch=2;condition=original[0]&15;displacement=(int8_t)original[1];
    } else if (length>=6 && original[0]==0x0f && original[1]>=0x80 && original[1]<=0x8f) {
        branch=6;condition=original[1]&15;memcpy(&displacement,original+2,4);
    } else return 0;
    uint64_t next=site+branch,target;
    if (displacement<0) {
        if (next<(uint64_t)(-(int64_t)displacement)) return 0;
        target=next-(uint64_t)(-(int64_t)displacement);
    } else {
        if (next>UINT64_MAX-(uint32_t)displacement) return 0;
        target=next+(uint32_t)displacement;
    }
    /* Do not permit a target inside the overwritten instruction range. */
    if (target>=site && target<site+length) return 0;
    uint8_t bytes[48]={0x9c,0x48,0x83,0x3d}; /* save incoming flags */
    if (!relative(bytes+4,slot,code+9)) return 0;
    bytes[8]=0;bytes[9]=0x74;bytes[10]=6; /* unset -> original flags/Jcc */
    bytes[11]=0x9d;bytes[12]=0xe9;
    /* For forced fall-through, retain any overwritten following instructions. */
    uint64_t enabled_target=force_taken ? target : code+24;
    if (!relative(bytes+13,enabled_target,code+17)) return 0;
    bytes[17]=0x9d;bytes[18]=0x0f;bytes[19]=0x80 | condition;
    if (!relative(bytes+20,target,code+24)) return 0;
    memcpy(bytes+24,original+branch,length-branch);
    size_t n=24+length-branch;bytes[n++]=0xe9;
    if (!relative(bytes+n,site+length,code+n+4)) return 0;
    n+=4;memcpy(output,bytes,n);return n;
}
