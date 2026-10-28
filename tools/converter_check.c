#include "../source/converter_process.h"
#include <stdio.h>
int main(int argc,char **argv) {
    if (argc!=5) return 2;
    char log[32768];
    int result=bo3_run_converter(argv[1],argv[2],argv[3],argv[4],150000,log,sizeof(log));
    printf("%s",log);fprintf(stderr,"Converter status: %d\n",result);
    return result<0 ? 124 : result;
}
