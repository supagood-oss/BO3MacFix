#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "../source/converter_process.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static unsigned long long now(void) {
    struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (unsigned long long)t.tv_sec*1000+(unsigned long long)t.tv_nsec/1000000;
}
static void script(const char *path,const char *text) {
    FILE *f=fopen(path,"w");assert(f);assert(fputs(text,f)>=0);assert(fclose(f)==0);assert(chmod(path,0700)==0);
}
int main(void) {
    char root[]="/tmp/bo3 converter XXXXXX";assert(mkdtemp(root));
    char shim[1024],log[256];snprintf(shim,sizeof(shim),"%s/fake wine",root);
    assert(setenv("BO3_TEST_EXE","converter ; literal",1)==0);
    assert(setenv("BO3_TEST_INPUT","input $(literal) and 'quotes'",1)==0);
    assert(setenv("BO3_TEST_OUTPUT","output with spaces",1)==0);
    assert(setenv("DYLD_INSERT_LIBRARIES","must-not-reach-converter",1)==0);
    char large[10000];memset(large,'A',sizeof(large)-1);large[sizeof(large)-1]=0;
    assert(setenv("BO3_TEST_LARGE",large,1)==0 && setenv("WINEPREFIX","/tmp/fixture-prefix",1)==0);
    script(shim,"#!/bin/sh\n[ \"$1\" = 'converter ; literal' ] || exit 91\n"
        "[ \"$2\" = '-mac_navmesh' ] || exit 92\n[ \"$3\" = \"input \\$(literal) and 'quotes'\" ] || exit 93\n"
        "[ \"$4\" = 'output with spaces' ] || exit 94\n[ -z \"$DYLD_INSERT_LIBRARIES\" ] || exit 95\n"
        "[ -z \"$BO3_TEST_LARGE\" ] || exit 96\n[ \"$WINEPREFIX\" = '/tmp/fixture-prefix' ] || exit 97\n"
        "sleep 1 &\nprintf 'fixture pass\\n'\nexit 17\n");
    unsigned long long start=now();
    assert(bo3_run_converter(shim,getenv("BO3_TEST_EXE"),getenv("BO3_TEST_INPUT"),getenv("BO3_TEST_OUTPUT"),3000,log,sizeof(log))==17);
    assert(strstr(log,"fixture pass") && now()-start<900);
    assert(getenv("DYLD_INSERT_LIBRARIES") && !strcmp(getenv("DYLD_INSERT_LIBRARIES"),"must-not-reach-converter"));
    script(shim,"#!/bin/sh\nexec sleep 5\n");
    assert(bo3_run_converter(shim,"unused","input","output",50,log,sizeof(log))==-2);
    assert(bo3_run_converter("/no-such-bo3-fixture", "unused","input","output",50,log,sizeof(log))==-1);
    assert(unlink(shim)==0 && rmdir(root)==0);
    puts("Converter argument, environment, background-handle and timeout checks passed");return 0;
}
