#include "../source/t7o_codegen.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int32_t i32(const uint8_t *p) { int32_t v;memcpy(&v,p,4);return v; }
int main(void) {
    uint8_t output[64],old[64];memset(output,0xa5,sizeof(output));memcpy(old,output,sizeof(old));
    const uint8_t cmp[]={0x81,0xf9,0xdc,5,0,0};
    assert(!t7o_codegen_cmp32(output,16,0x2000,0x1000,0x3000,cmp,6));
    assert(!memcmp(output,old,sizeof(output)));
    size_t n=t7o_codegen_cmp32(output,sizeof(output),0x2000,0x1000,0x3000,cmp,6);
    assert(n==29 && output[11]==0x0d && i32(output+3)==0x3000-0x2008 &&
        i32(output+12)==0x3000-0x2010 && !memcmp(output+18,cmp,6));
    assert(0x2000+n+i32(output+n-4)==0x1006);
    const uint8_t branch[]={0x7d,0x2e,0x0f,0x57,0xc0};
    n=t7o_codegen_branch(output,sizeof(output),0x2000,0x1000,0x3000,branch,5,1);
    assert(n==32 && output[0]==0x9c && output[11]==0x9d && output[17]==0x9d);
    assert(0x2011+i32(output+13)==0x1030 && 0x2018+i32(output+20)==0x1030);
    assert(!memcmp(output+24,branch+2,3));
    n=t7o_codegen_branch(output,sizeof(output),0x2000,0x1000,0x3000,branch,5,0);
    assert(n==32 && 0x2011+i32(output+13)==0x2018);
    memset(output,0xa5,sizeof(output));memcpy(old,output,sizeof(old));
    assert(!t7o_codegen_branch(output,sizeof(output),0x2000,0x1000,UINT64_MAX,branch,5,1));
    assert(!memcmp(output,old,sizeof(output)));
    const uint8_t invalid[]={0x90,0x90,0x90,0x90,0x90};
    assert(!t7o_codegen_branch(output,sizeof(output),0x2000,0x1000,0x3000,invalid,5,1));
    assert(!t7o_codegen_cmp32(output,sizeof(output),0x2000,0x1000,0x3000,invalid,5));
    puts("Audited comparison and branch stub bounds/targets passed");
}
