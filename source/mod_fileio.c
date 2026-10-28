#include "mod_fileio.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p,0755)
#endif
int bo3_lua_path(const char *base,const char *input,char *out,size_t capacity) {
    if (!base || !input || !out || !capacity) { errno=EINVAL; return 0; }
    int n=snprintf(out,capacity,"%s%s",input[0]=='/' ? "" : base,input);
    if (n<0 || (size_t)n>=capacity) { out[0]=0; errno=ENAMETOOLONG; return 0; }
    for (char *p=out;*p;++p) if (*p=='\\') *p='/';
    return 1;
}
static int directory(const char *path) {
    struct stat st;
    return stat(path,&st)==0 && S_ISDIR(st.st_mode);
}
int bo3_mkdirs(const char *input) {
    char path[4096];
    if (!input || !*input || strlen(input)>=sizeof(path)) { errno=EINVAL; return 0; }
    strcpy(path,input);
    for (char *p=path;*p;++p) if (*p=='\\') *p='/';
    char *start=path+1;
#ifdef _WIN32
    if (path[0] && path[1]==':' && path[2]=='/') start=path+3;
#endif
    for (char *p=start;;++p) {
        if (*p=='/' || !*p) {
            char saved=*p; *p=0;
            int ok=directory(path) || make_dir(path)==0;
            *p=saved;
            if (!ok) return 0;
            if (!saved) return 1;
        }
    }
}
int bo3_prepare_save(const char *base,const char *input,const char *mode) {
    char normalized[4096],path[4096];
    if (!input || !mode || (mode[0]!='w' && mode[0]!='a' && mode[0]!='x')) return 1;
    if (!bo3_lua_path("",input,normalized,sizeof(normalized))) return 0;
    const char *relative=normalized;
    while (!strncmp(relative,"./",2)) relative+=2;
    if (strncmp(relative,"players/",8)) return 1;
    const char *part=relative;
    while (*part) {
        const char *end=strchr(part,'/'); size_t n=end ? (size_t)(end-part) : strlen(part);
        if (n==2 && !strncmp(part,"..",2)) return 1;
        if (!end) break;
        part=end+1;
    }
    if (!bo3_lua_path(base,relative,path,sizeof(path))) return 0;
    char *last=strrchr(path,'/');
    if (!last) return 1;
    *last=0;
    return bo3_mkdirs(path);
}
