#include "t7o_save_codec.h"
#include <stdlib.h>
#include <string.h>
static uint64_t le(const unsigned char *p,unsigned n) {
    uint64_t v=0;
    while (n) v=(v<<8)|p[--n];
    return v;
}
int t7o_save_keys(const unsigned char *dll,size_t size,T7oSaveKey keys[2]) {
    if (!dll || !keys || size<64 || memcmp(dll,"MZ",2)) return 0;
    size_t pe=(size_t)le(dll+60,4);
    if (pe>size || size-pe<24 || memcmp(dll+pe,"PE\0\0",4)) return 0;
    size_t count=(size_t)le(dll+pe+6,2),table=pe+24+(size_t)le(dll+pe+20,2);
    if (table>size || count>(size-table)/40) return 0;
    for (size_t i=0;i<count;++i) {
        const unsigned char *s=dll+table+40*i;
        uint64_t rva=le(s+12,4),raw_size=le(s+16,4),offset=le(s+20,4);
        const uint64_t target=0x5f810;
        if (target<rva || target-rva>raw_size || raw_size-(target-rva)<32) continue;
        offset+=target-rva;
        if (offset>size || size-offset<32) return 0;
        for (unsigned j=0;j<2;++j) {
            keys[j]=(T7oSaveKey){le(dll+offset+16*j,8),le(dll+offset+16*j+8,8)};
            if (keys[j].seed>UINT32_MAX || keys[j].factor>65535) return 0;
        }
        return 1;
    }
    return 0;
}
static int key_ok(T7oSaveKey key) { return key.seed<=UINT32_MAX && key.factor<=65535; }
char *t7o_save_encode(const char *text,T7oSaveKey key) {
    static const char digits[]="0123456789abcdef";
    if (!text || !key_ok(key)) return NULL;
    size_t n=strlen(text);
    if (n>(SIZE_MAX-1)/2) return NULL;
    char *out=malloc(n*2+1); if (!out) return NULL;
    uint64_t state=key.seed;
    for (size_t i=0;i<n;++i) {
        uint64_t high=state/512,low=state%512;
        unsigned odd=(unsigned)(high%128)*2+1,inv=1;
        while ((odd*inv)%256!=1) inv+=2;
        unsigned plain=(unsigned char)text[i];
        unsigned encoded=(unsigned)((inv*plain-high/128)&255);
        state=low*(key.factor+16384)+encoded+plain+high;
        out[2*i]=digits[encoded>>4]; out[2*i+1]=digits[encoded&15];
    }
    out[2*n]=0; return out;
}
static int nibble(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
char *t7o_save_decode(const char *hex,const T7oSaveKey keys[2]) {
    if (!hex || !keys) return NULL;
    size_t n=strlen(hex);
    if (n%2) return NULL;
    for (size_t i=0;i<n;++i) if (nibble(hex[i])<0) return NULL;
    char *out=malloc(n/2+1); if (!out) return NULL;
    for (unsigned generation=0;generation<2;++generation) {
        if (!key_ok(keys[generation])) continue;
        uint64_t state=keys[generation].seed;
        for (size_t i=0;i<n/2;++i) {
            unsigned encoded=(unsigned)(nibble(hex[2*i])*16+nibble(hex[2*i+1]));
            uint64_t high=state/512,low=state%512;
            unsigned plain=(unsigned)((encoded+high/128)*((high%128)*2+1)&255);
            out[i]=(char)plain;
            state=low*(keys[generation].factor+16384)+encoded+plain+high;
        }
        out[n/2]=0;
        /* The Windows wrapper tries both key generations and accepts a JSON
         * object. Invalid input never becomes a new plaintext save. */
        if (n && out[0]=='{' && !memchr(out,0,n/2)) return out;
    }
    free(out); return NULL;
}
