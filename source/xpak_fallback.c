#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "xpak_fallback.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifdef _WIN32
#define resolve_path(p, out) _fullpath(out, p, PATH_MAX)
#define seek_file _fseeki64
#else
#define resolve_path(p, out) realpath(p, out)
#define seek_file fseeko
#endif
typedef struct { uint64_t key, offset, size; } Entry;
typedef struct {
    FILE *file;
    uint64_t length, data, data_size, refs, ref_size, names, name_size, name_count;
    size_t count, ref_count;
    Entry *entries;
    uint64_t *keys;
} Pack;
static uint64_t u64(const unsigned char *p) {
    uint64_t value=0;
    for (int i=7;i>=0;--i) value=(value<<8)|p[i];
    return value;
}
static int range(uint64_t start, uint64_t size, uint64_t length) {
    return start<=length && size<=length-start;
}
static int read_at(FILE *f, uint64_t offset, void *out, size_t n) {
    return offset<=INT64_MAX && seek_file(f,(int64_t)offset,SEEK_SET)==0 && fread(out,1,n,f)==n;
}
static int order(const void *a,const void *b) {
    uint64_t x=((const Entry *)a)->key,y=((const Entry *)b)->key;
    return (x>y)-(x<y);
}
static const Entry *find(const Pack *p,uint64_t key) {
    Entry item={key,0,0};
    return bsearch(&item,p->entries,p->count,sizeof(Entry),order);
}
static void close_pack(Pack *p) {
    if (p->file) fclose(p->file);
    free(p->entries); free(p->keys); memset(p,0,sizeof(*p));
}
static int open_pack(const char *path, Pack *p) {
    unsigned char h[120],raw[24]; struct stat st;
    memset(p,0,sizeof(*p));
    if (stat(path,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<120) return 0;
    p->length=(uint64_t)st.st_size;
    p->file=fopen(path,"rb");
    if (!p->file || !read_at(p->file,0,h,sizeof(h)) || memcmp(h,"KAPI",4) ||
        h[6]!=10 || h[7]!=0 || u64(h+16)!=p->length) goto bad;
    uint64_t count=u64(h+48), refs=u64(h+72), table=u64(h+56);
    p->data=u64(h+32); p->data_size=u64(h+40);
    p->refs=u64(h+80); p->ref_size=u64(h+88);
    p->names=u64(h+104); p->name_size=u64(h+112); p->name_count=u64(h+96);
    /* Counts are bounded before multiplication or allocation. */
    if (count>1000000 || refs>1000000 || u64(h+24)!=count ||
        u64(h+64)!=count*24 || p->ref_size!=refs*8 ||
        !range(table,count*24,p->length) || !range(p->data,p->data_size,p->length) ||
        !range(p->refs,p->ref_size,p->length) || !range(p->names,p->name_size,p->length)) goto bad;
    p->count=(size_t)count; p->ref_count=(size_t)refs;
    p->entries=calloc(p->count ? p->count : 1,sizeof(Entry));
    p->keys=calloc(p->ref_count ? p->ref_count : 1,sizeof(uint64_t));
    if (!p->entries || !p->keys) goto bad;
    for (size_t i=0;i<p->count;++i) {
        if (!read_at(p->file,table+i*24,raw,24)) goto bad;
        p->entries[i]=(Entry){u64(raw),u64(raw+8),u64(raw+16)};
        if (!range(p->entries[i].offset,p->entries[i].size,p->data_size)) goto bad;
    }
    qsort(p->entries,p->count,sizeof(Entry),order);
    for (size_t i=1;i<p->count;++i) if (p->entries[i-1].key==p->entries[i].key) goto bad;
    for (size_t i=0;i<p->ref_count;++i) {
        if (!read_at(p->file,p->refs+i*8,raw,8)) goto bad;
        p->keys[i]=u64(raw);
    }
    return 1;
bad: close_pack(p); return 0;
}
static int equal_bytes(Pack *a,uint64_t x,Pack *b,uint64_t y,uint64_t length) {
    unsigned char left[32768],right[32768];
    while (length) {
        size_t n=length>sizeof(left) ? sizeof(left) : (size_t)length;
        if (!read_at(a->file,x,left,n) || !read_at(b->file,y,right,n) || memcmp(left,right,n)) return 0;
        x+=n; y+=n; length-=n;
    }
    return 1;
}
static int missing_are_meshes(Pack *p) {
    uint64_t pos=p->names,end=pos+p->name_size;
    size_t matched=0,missing=0;
    for (size_t i=0;i<p->ref_count;++i) if (!find(p,p->keys[i])) ++missing;
    if (!missing) return 0;
    for (uint64_t i=0;i<p->name_count;++i) {
        unsigned char h[16];
        if (!range(pos,16,end) || !read_at(p->file,pos,h,16)) return 0;
        uint64_t key=u64(h),length=u64(h+8); pos+=16;
        if (!range(pos,length,end) || length>65536) return 0;
        if (!find(p,key)) {
            int referenced=0;
            for (size_t j=0;j<p->ref_count;++j) if (p->keys[j]==key) { referenced=1; break; }
            if (referenced) {
                char *text=calloc((size_t)length+2,1);
                if (!text) return 0;
                int ok=read_at(p->file,pos,text,(size_t)length);
                if (ok) {
                    char *line=text;
                    ok=0;
                    while (*line) {
                        char *next=strchr(line,'\n'); if (next) *next=0;
                        size_t n=strlen(line); if (n && line[n-1]=='\r') line[n-1]=0;
                        if (!strcmp(line,"type: mesh")) ok=1;
                        if (!next) break;
                        line=next+1;
                    }
                }
                free(text); if (!ok) return 0;
                ++matched;
            }
        }
        pos+=length;
    }
    return missing && matched==missing && pos==end;
}
static int completes(Pack *a,Pack *b) {
    if (a->count>=b->count || a->ref_count!=b->ref_count || a->name_count!=b->name_count ||
        a->name_size!=b->name_size || !equal_bytes(a,a->refs,b,b->refs,a->ref_size) ||
        !equal_bytes(a,a->names,b,b->names,a->name_size)) return 0;
    for (size_t i=0;i<a->ref_count;++i) if (!find(b,a->keys[i])) return 0;
    for (size_t i=0;i<a->count;++i) {
        const Entry *other=find(b,a->entries[i].key);
        if (!other || other->size!=a->entries[i].size ||
            !equal_bytes(a,a->data+a->entries[i].offset,b,b->data+other->offset,other->size)) return 0;
    }
    return 1;
}
typedef struct { char source[PATH_MAX],target[PATH_MAX]; struct stat a,b; } Cached;
static Cached cache[8]; static unsigned next_cache;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int same_stat(const struct stat *a,const struct stat *b) {
    if (a->st_dev!=b->st_dev || a->st_ino!=b->st_ino || a->st_size!=b->st_size ||
        a->st_mtime!=b->st_mtime || a->st_ctime!=b->st_ctime) return 0;
#if defined(__APPLE__)
    return a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec && a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;
#elif !defined(_WIN32)
    return a->st_mtim.tv_nsec==b->st_mtim.tv_nsec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
#else
    return 1;
#endif
}
int bo3_xpak_fallback(const char *path,char *out,size_t capacity) {
    static const char *languages[]={"ge","en","fr","it","sp","ru","po","bp","jp","cn","tc","ko","ar"};
    char normalized[PATH_MAX],source[PATH_MAX],target[PATH_MAX]; struct stat sa,sb;
    Pack a={0},b={0}; int ok=0;
    if (!path || !out || !capacity || strlen(path)>=sizeof(normalized)) return 0;
    strcpy(normalized,path);
    for (char *p=normalized;*p;++p) if (*p=='\\') *p='/';
    const char *name=strrchr(normalized,'/'); name=name ? name+1 : normalized;
    size_t n=strlen(name);
    if (n<9 || name[2]!='_' || strcmp(name+n-5,".xpak") ||
        name[0]<'a' || name[0]>'z' || name[1]<'a' || name[1]>'z') return 0;
    if (!resolve_path(normalized,source)) return 0;
    for (char *p=source;*p;++p) if (*p=='\\') *p='/';
    if (!strstr(source,"/workshop/content/311210/") && !strstr(source,"/usermaps/") && !strstr(source,"/mods/")) return 0;
    if (stat(source,&sa)!=0) return 0;
    pthread_mutex_lock(&lock);
    for (size_t i=0;i<8;++i) if (!strcmp(cache[i].source,source) && same_stat(&cache[i].a,&sa) &&
        stat(cache[i].target,&sb)==0 && same_stat(&cache[i].b,&sb)) {
        if (strlen(cache[i].target)<capacity) { strcpy(out,cache[i].target); ok=1; }
        goto done;
    }
    if (!open_pack(source,&a) || !missing_are_meshes(&a)) goto done;
    strcpy(target,source); char *base=strrchr(target,'/');
    if (!base || strlen(base+1)!=n) goto done;
    ++base;
    for (size_t i=0;i<sizeof(languages)/sizeof(*languages);++i) {
        memcpy(base,languages[i],2);
        if (!strcmp(target,source) || stat(target,&sb)!=0 || !open_pack(target,&b)) continue;
        int valid=completes(&a,&b); close_pack(&b);
        struct stat after,after_target;
        if (valid && stat(source,&after)==0 && same_stat(&sa,&after) && stat(target,&after_target)==0 && same_stat(&sb,&after_target) && strlen(target)<capacity) {
            Cached *entry=&cache[next_cache++%8];
            strcpy(entry->source,source); strcpy(entry->target,target); entry->a=sa; entry->b=sb;
            strcpy(out,target); ok=1; break;
        }
    }
done:
    close_pack(&a); close_pack(&b); pthread_mutex_unlock(&lock); return ok;
}
