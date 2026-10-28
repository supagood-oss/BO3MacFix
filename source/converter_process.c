#define _DARWIN_C_SOURCE 1
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "converter_process.h"
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <crt_externs.h>
#define PROCESS_ENVIRON (*_NSGetEnviron())
#else
extern char **environ;
#define PROCESS_ENVIRON environ
#endif

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC,&now)!=0) return UINT64_MAX;
    return (uint64_t)now.tv_sec*1000+(uint64_t)now.tv_nsec/1000000;
}
int bo3_run_converter(const char *wine,const char *converter,const char *input,
    const char *output,unsigned timeout_ms,char *log,size_t capacity) {
    if (log && capacity) log[0]=0;
    if (!wine || !converter || !input || !output || !timeout_ms) return -1;
    FILE *capture=tmpfile();if (!capture) return -1;
    char **environment=PROCESS_ENVIRON;size_t count=0;
    while (environment[count]) ++count;
    char **child_environment=calloc(count+3,sizeof(char *));
    if (!child_environment) { fclose(capture);return -1; }
    char resolved[PATH_MAX],candidate[PATH_MAX],frameworks[PATH_MAX],support[PATH_MAX+80];bool private_support=false;
    if (realpath(wine,resolved)) {
        char *slash=strrchr(resolved,'/');if (slash) *slash=0;
        if (slash && snprintf(candidate,sizeof(candidate),"%s/../../Frameworks",resolved)<(int)sizeof(candidate) && realpath(candidate,frameworks)) {
            struct stat info;
            if (stat(frameworks,&info)==0 && S_ISDIR(info.st_mode)) {
                snprintf(support,sizeof(support),"DYLD_FALLBACK_LIBRARY_PATH=%s:/usr/lib",frameworks);private_support=true;
            }
        }
    }
    /* Wine 11 can hang in prefix setup/exit with large inherited environments
     * (WineHQ 60185). The converter needs only these bounded runtime settings,
     * not the game's injection variables, Steam state or CI credentials. */
    static const char *allowed[]={"HOME=","USER=","LOGNAME=","TMPDIR=","LANG=","LC_ALL=","LC_CTYPE=",
        "__CF_USER_TEXT_ENCODING=","DISPLAY=","XAUTHORITY=","WINEPREFIX=","WINEARCH=","WINEDEBUG=",
        "WINEDLLOVERRIDES=","WINESERVER=","WINELOADER=","DYLD_FALLBACK_LIBRARY_PATH=","DYLD_LIBRARY_PATH=",
        "MVK_CONFIG_LOG_LEVEL="};
    size_t kept=0;
    for (size_t i=0;i<count;++i) {
        for (size_t key=0;key<sizeof(allowed)/sizeof(*allowed);++key)
            if (strncmp(environment[i],allowed[key],strlen(allowed[key]))==0) {
                if (private_support && !strcmp(allowed[key],"DYLD_FALLBACK_LIBRARY_PATH=")) break;
                if (strlen(environment[i])>1024) { free(child_environment);fclose(capture);return -1; }
                child_environment[kept++]=environment[i];break;
            }
    }
    child_environment[kept++]="PATH=/usr/bin:/bin:/usr/sbin:/sbin";
    if (private_support) child_environment[kept++]=support;
    child_environment[kept]=NULL;
    char *arguments[]={(char *)wine,(char *)converter,"-mac_navmesh",(char *)input,(char *)output,NULL};
    posix_spawn_file_actions_t actions;
    int error=posix_spawn_file_actions_init(&actions);
    if (error) { free(child_environment);fclose(capture);return -1; }
    int fd=fileno(capture);
    error=posix_spawn_file_actions_adddup2(&actions,fd,STDOUT_FILENO);
    if (!error) error=posix_spawn_file_actions_adddup2(&actions,fd,STDERR_FILENO);
    if (!error && fd>STDERR_FILENO) error=posix_spawn_file_actions_addclose(&actions,fd);
    pid_t pid=0;
    if (!error) error=posix_spawnp(&pid,wine,&actions,NULL,arguments,child_environment);
    posix_spawn_file_actions_destroy(&actions);free(child_environment);
    if (error) { fclose(capture);return -1; }
    int status=0,result=-1;uint64_t start=milliseconds();
    for (;;) {
        pid_t ended=waitpid(pid,&status,WNOHANG);
        if (ended==pid) { result=WIFEXITED(status) ? WEXITSTATUS(status) : -1;break; }
        if (ended<0 && errno!=EINTR) break;
        uint64_t now=milliseconds();
        if (start==UINT64_MAX || now==UINT64_MAX || now-start>=timeout_ms) {
            /* This child has not been reaped, so its PID cannot be reused. */
            kill(pid,SIGKILL);
            while (waitpid(pid,&status,0)<0 && errno==EINTR) {}
            result=-2;break;
        }
        struct timespec pause={0,10000000};nanosleep(&pause,NULL);
    }
    if (log && capacity) {
        rewind(capture);size_t length=fread(log,1,capacity-1,capture);log[length]=0;
    }
    fclose(capture);return result;
}
