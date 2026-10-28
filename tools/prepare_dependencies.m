#import <Foundation/Foundation.h>
#import "../installer/SetupDependencies.h"
#include <string.h>
// CI exercises the exact same downloader/unpacker as the graphical installer.
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc==2 && !strcmp(argv[1],"--print-arch")) {
            puts([BO3SetupDependencies runtimeArchitecture].UTF8String);return 0;
        }
        if (argc!=3) return 2;
        NSError *error=nil;
        NSURL *payload=[NSURL fileURLWithPath:@(argv[1])], *output=[NSURL fileURLWithPath:@(argv[2])];
        NSURL *prepared=[BO3SetupDependencies prepare:payload progress:^(NSString *message) {
            printf("%s\n",message.UTF8String);
        } error:&error];
        if (!prepared || [prepared isEqual:payload] || ![NSFileManager.defaultManager moveItemAtURL:prepared toURL:output error:&error]) {
            fprintf(stderr,"%s\n",error.localizedDescription.UTF8String ?: "Dependencies were not prepared"); return 1;
        }
        [NSFileManager.defaultManager removeItemAtURL:prepared.URLByDeletingLastPathComponent error:nil];
        return 0;
    }
}
