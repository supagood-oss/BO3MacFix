#include "t7o_checkpoint_store.h"
#include "mod_fileio.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define sync_file _commit
#else
#define sync_file fsync
#endif

static const uint8_t build_id[32]={0x93,0x2f,0x09,0x18,0xfb,0x98,0x71,0xbc,0x62,0x5b,0x69,0xa5,0x87,0x13,0x5a,0x5c,
    0x29,0x76,0xe9,0xf9,0xe6,0xb7,0x28,0x9c,0xfa,0xdd,0xa8,0xab,0x99,0x6e,0x1c,0xd0};
static uint32_t u32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static uint64_t u64(const void *p) { uint64_t v;memcpy(&v,p,8);return v; }
static void put32(void *p,uint32_t v) { memcpy(p,&v,4); }
static void put64(void *p,uint64_t v) { memcpy(p,&v,8); }
static uint64_t hash(const void *bytes,size_t size) {
    const uint8_t *p=bytes;uint64_t h=UINT64_C(14695981039346656037);
    for (size_t i=0;i<size;++i) h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}
static bool component(const char *text,size_t capacity) {
    size_t n=0;
    for (;n<capacity && text[n];++n) {
        unsigned char c=(unsigned char)text[n];
        if (c<32 || c==127 || strchr("/\\:*?\"<>|",c)) return false;
    }
    return n>0 && n<capacity && strcmp(text,".") && strcmp(text,"..");
}
bool t7o_checkpoint_identity_valid(const T7oCheckpointIdentity *i) {
    return i && i->owner && i->slot>=1 && i->slot<=6 && i->max_clients>=1 && i->max_clients<=18 &&
        component(i->map,sizeof(i->map)) && component(i->folder,sizeof(i->folder)) && memchr(i->mod,0,sizeof(i->mod));
}
bool t7o_checkpoint_identity_equal(const T7oCheckpointIdentity *a,const T7oCheckpointIdentity *b) {
    return t7o_checkpoint_identity_valid(a) && t7o_checkpoint_identity_valid(b) && a->owner==b->owner &&
        a->slot==b->slot && a->max_clients==b->max_clients && !strcmp(a->map,b->map) && !strcmp(a->mod,b->mod) && !strcmp(a->folder,b->folder);
}
bool t7o_checkpoint_chunks_valid(const void *bytes,size_t size) {
    const uint8_t *p=bytes;size_t at=0;unsigned blocks=0;
    if (!p || size<4) return false;
    while (at<size) {
        if (size-at<4 || ++blocks>256) return false;
        uint32_t length=u32(p+at);
        if (length<4 || length>size-at) return false;
        at+=length;
    }
    return at==size;
}
bool t7o_checkpoint_header(uint8_t h[T7O_CP_HEADER],const T7oCheckpointImage *i) {
    if (!h || !i || !t7o_checkpoint_identity_valid(&i->identity) || i->size[0]>T7O_CP_SERVER_MAX || i->size[1]>T7O_CP_CLIENT_MAX ||
        !t7o_checkpoint_chunks_valid(i->payload[0],i->size[0]) || !t7o_checkpoint_chunks_valid(i->payload[1],i->size[1])) return false;
    memset(h,0,T7O_CP_HEADER);memcpy(h,"BO3MCP1",8);put32(h+8,1);put32(h+12,T7O_CP_HEADER);memcpy(h+16,build_id,32);
    put64(h+48,i->identity.owner);put32(h+56,i->identity.max_clients);put32(h+60,i->identity.slot);put64(h+64,i->timestamp);
    for (unsigned n=0;n<2;++n) { put32(h+72+n*4,i->size[n]);put64(h+80+n*8,hash(i->payload[n],i->size[n])); }
    put64(h+96,hash(i->camera,T7O_CP_CAMERA));
    memcpy(h+112,i->identity.map,strlen(i->identity.map));memcpy(h+176,i->identity.mod,strlen(i->identity.mod));
    memcpy(h+304,i->identity.folder,strlen(i->identity.folder));put32(h+496,T7O_CP_CAMERA);memcpy(h+512,i->camera,T7O_CP_CAMERA);
    put64(h+104,hash(h,T7O_CP_HEADER));return true;
}
bool t7o_checkpoint_decode(const uint8_t h[T7O_CP_HEADER],T7oCheckpointImage *i) {
    uint8_t copy[T7O_CP_HEADER];
    if (!h || !i || memcmp(h,"BO3MCP1",8) || u32(h+8)!=1 || u32(h+12)!=T7O_CP_HEADER || memcmp(h+16,build_id,32) ||
        u32(h+496)!=T7O_CP_CAMERA || !memchr(h+112,0,64) || !memchr(h+176,0,128) || !memchr(h+304,0,192)) return false;
    memcpy(copy,h,sizeof(copy));put64(copy+104,0);if (hash(copy,sizeof(copy))!=u64(h+104) || hash(h+512,T7O_CP_CAMERA)!=u64(h+96)) return false;
    T7oCheckpointImage result={0};result.identity.owner=u64(h+48);result.identity.max_clients=u32(h+56);result.identity.slot=u32(h+60);
    result.timestamp=u64(h+64);result.size[0]=u32(h+72);result.size[1]=u32(h+76);
    memcpy(result.identity.map,h+112,64);memcpy(result.identity.mod,h+176,128);memcpy(result.identity.folder,h+304,192);memcpy(result.camera,h+512,T7O_CP_CAMERA);
    if (!t7o_checkpoint_identity_valid(&result.identity) || result.size[0]<4 || result.size[0]>T7O_CP_SERVER_MAX ||
        result.size[1]<4 || result.size[1]>T7O_CP_CLIENT_MAX) return false;
    *i=result;return true;
}
bool t7o_checkpoint_payload_valid(const uint8_t h[T7O_CP_HEADER],const T7oCheckpointImage *i) {
    T7oCheckpointImage decoded;
    if (!i || !t7o_checkpoint_decode(h,&decoded) || !t7o_checkpoint_identity_equal(&i->identity,&decoded.identity) ||
        memcmp(i->size,decoded.size,sizeof(i->size)) || memcmp(i->camera,decoded.camera,T7O_CP_CAMERA)) return false;
    for (unsigned n=0;n<2;++n) if (!t7o_checkpoint_chunks_valid(i->payload[n],i->size[n]) || hash(i->payload[n],i->size[n])!=u64(h+80+n*8)) return false;
    return true;
}
void t7o_checkpoint_dispose(T7oCheckpointImage *i) {
    if (i) { free(i->payload[0]);free(i->payload[1]);memset(i,0,sizeof(*i)); }
}
bool t7o_checkpoint_read(const char *path,T7oCheckpointImage *out) {
    uint8_t h[T7O_CP_HEADER];T7oCheckpointImage image={0};FILE *f=path ? fopen(path,"rb") : NULL;bool ok=false;
    if (!f) return false;
    if (!out) { fclose(f);return false; }
    if (fread(h,1,sizeof(h),f)!=sizeof(h) || !t7o_checkpoint_decode(h,&image)) goto done;
    for (unsigned n=0;n<2;++n) {
        image.payload[n]=malloc(image.size[n]);
        if (!image.payload[n] || fread(image.payload[n],1,image.size[n],f)!=image.size[n]) goto done;
    }
    if (fgetc(f)!=EOF || ferror(f) || !t7o_checkpoint_payload_valid(h,&image)) goto done;
    ok=true;
done:
    if (fclose(f)) ok=false;
    if (ok) *out=image;else t7o_checkpoint_dispose(&image);
    return ok;
}
bool t7o_checkpoint_write(const char *path,const T7oCheckpointImage *image) {
    uint8_t h[T7O_CP_HEADER];char temporary[4096],backup[4096];bool ok=false;
    if (!path || !t7o_checkpoint_header(h,image) || snprintf(temporary,sizeof(temporary),"%s.tmp",path)>=(int)sizeof(temporary) ||
        snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup)) return false;
    /* A stale temp file is safe to remove: it is never an authoritative save.
     * Exclusive creation prevents following a replacement symlink. */
    unlink(temporary);int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL
#ifdef _WIN32
        |O_BINARY
#endif
        ,0600);
    if (fd<0) return false;
    FILE *f=fdopen(fd,"wb");if (!f) { close(fd);unlink(temporary);return false; }
    ok=fwrite(h,1,sizeof(h),f)==sizeof(h);
    for (unsigned n=0;n<2 && ok;++n) ok=fwrite(image->payload[n],1,image->size[n],f)==image->size[n];
    if (ok) ok=fflush(f)==0 && sync_file(fileno(f))==0;
    if (fclose(f)) ok=false;
    if (ok) {
#ifdef _WIN32
        if (GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES && !CopyFileA(path,backup,FALSE)) ok=false;
        if (ok) ok=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        struct stat st;
        if (lstat(path,&st)==0) {
            if (!S_ISREG(st.st_mode)) ok=false;
            else { unlink(backup);if (link(path,backup)) ok=false; }
        } else if (errno!=ENOENT) ok=false;
        if (ok) ok=rename(temporary,path)==0;
#endif
    }
    if (!ok) unlink(temporary);
    return ok;
}
bool t7o_checkpoint_path(const char *root,const T7oCheckpointIdentity *i,char *out,size_t capacity) {
    if (!root || !*root || !out || !capacity || !t7o_checkpoint_identity_valid(i)) return false;
    int n=snprintf(out,capacity,"%s/%s/%016" PRIx64 "_%016" PRIx64 "_%s_%u.macsav",root,i->folder,i->owner,hash(i->mod,strlen(i->mod)),i->map,i->slot);
    return n>=0 && (size_t)n<capacity;
}
