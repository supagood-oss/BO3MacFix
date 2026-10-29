#include "../source/t7o_checkpoint_store.h"
#include "../source/mod_fileio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#endif
static void put32(void *p,uint32_t value) { memcpy(p,&value,4); }
static T7oCheckpointImage fixture(void) {
    T7oCheckpointImage i={.identity={.owner=123456789,.max_clients=4,.slot=2,.map="zm_test",.mod="usermaps/AAE",.folder="fixture"},.timestamp=1700000000,.size={20,12}};
    put32(i.camera,1);
    for (unsigned n=0;n<2;++n) { i.payload[n]=calloc(1,i.size[n]);assert(i.payload[n]);put32(i.payload[n],8);put32(i.payload[n]+8,i.size[n]-8); }
    return i;
}
int main(int argc,char **argv) {
    T7oCheckpointImage i=fixture(),read={0};uint8_t h[T7O_CP_HEADER],bad[T7O_CP_HEADER];char path[4096],backup[4100],temporary[4100],directory[4096];
    if (argc==3) {
        if (!strcmp(argv[1],"write")) assert(t7o_checkpoint_write(argv[2],&i));
        else { assert(!strcmp(argv[1],"read") && t7o_checkpoint_read(argv[2],&read));assert(read.identity.owner==i.identity.owner && read.timestamp==i.timestamp && !memcmp(read.payload[0],i.payload[0],i.size[0])); }
        t7o_checkpoint_dispose(&read);t7o_checkpoint_dispose(&i);return 0;
    }
    snprintf(directory,sizeof(directory),"build/checkpoint-store-%lu",(unsigned long)getpid());assert(bo3_mkdirs(directory));
    assert(t7o_checkpoint_identity_valid(&i.identity) && t7o_checkpoint_header(h,&i));
    assert(t7o_checkpoint_decode(h,&read) && t7o_checkpoint_identity_equal(&i.identity,&read.identity));memset(&read,0,sizeof(read));
    for (unsigned offset=0;offset<T7O_CP_HEADER;++offset) { memcpy(bad,h,sizeof(bad));bad[offset]^=1;assert(!t7o_checkpoint_decode(bad,&read)); }
    assert(t7o_checkpoint_payload_valid(h,&i));i.payload[0][7]=1;assert(!t7o_checkpoint_payload_valid(h,&i));i.payload[0][7]=0;
    uint8_t chunks[16]={0};put32(chunks,8);put32(chunks+8,8);assert(t7o_checkpoint_chunks_valid(chunks,16));
    for (unsigned n=0;n<16;++n) { put32(chunks,n);if (n!=8) assert(!t7o_checkpoint_chunks_valid(chunks,16)); }put32(chunks,8);
    assert(!t7o_checkpoint_chunks_valid(chunks,15));
    strcpy(i.identity.folder,"../elsewhere");assert(!t7o_checkpoint_identity_valid(&i.identity));strcpy(i.identity.folder,"fixture");
    assert(t7o_checkpoint_path(directory,&i.identity,path,sizeof(path)) && !t7o_checkpoint_path(directory,&i.identity,path,5));
    assert(t7o_checkpoint_path(directory,&i.identity,path,sizeof(path)));
    char *slash=strrchr(path,'/');assert(slash);*slash=0;assert(bo3_mkdirs(path));*slash='/';
    snprintf(backup,sizeof(backup),"%s.bak",path);snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    assert(t7o_checkpoint_write(path,&i) && t7o_checkpoint_read(path,&read));assert(read.size[0]==20 && !memcmp(read.camera,i.camera,T7O_CP_CAMERA));t7o_checkpoint_dispose(&read);
    ++i.timestamp;assert(t7o_checkpoint_write(path,&i) && t7o_checkpoint_read(backup,&read));assert(read.timestamp+1==i.timestamp);t7o_checkpoint_dispose(&read);
    /* A leftover temporary file cannot replace or damage the authoritative one. */
    FILE *f=fopen(temporary,"wb");assert(f);fputs("interrupted write",f);fclose(f);assert(t7o_checkpoint_read(path,&read));t7o_checkpoint_dispose(&read);
    assert(t7o_checkpoint_write(path,&i));f=fopen(path,"ab");assert(f);fputc(0,f);fclose(f);assert(!t7o_checkpoint_read(path,&read));
    f=fopen(path,"wb");assert(f);assert(fwrite(h,1,sizeof(h)-1,f)==sizeof(h)-1);fclose(f);assert(!t7o_checkpoint_read(path,&read));
    assert(t7o_checkpoint_read(backup,&read));t7o_checkpoint_dispose(&read);
    unlink(path);unlink(backup);unlink(temporary);*slash=0;rmdir(path);rmdir(directory);t7o_checkpoint_dispose(&i);
    puts("Checkpoint store: header mutation, checksum, chunk bounds, traversal rejection, atomic replacement, backup and interrupted/truncated/trailing-file checks passed");
}
