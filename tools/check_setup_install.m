#import <Foundation/Foundation.h>
#import "../installer/SetupEngine.h"
#include <sys/stat.h>
#include <stdlib.h>

static void require(BOOL ok, NSError *error) {
    if (!ok) { fprintf(stderr,"Installer integration check failed: %s\n",error.localizedDescription.UTF8String ?: "assertion");exit(1); }
}
static NSURL *at(NSURL *root,NSString *path) { return [root URLByAppendingPathComponent:path]; }
static void writeFixture(NSURL *file,NSString *text) {
    NSError *error=nil;
    require([NSFileManager.defaultManager createDirectoryAtURL:file.URLByDeletingLastPathComponent
        withIntermediateDirectories:YES attributes:nil error:&error],error);
    require([text writeToURL:file atomically:YES encoding:NSUTF8StringEncoding error:&error],error);
}
// Run on CI with real downloaded helpers but a synthetic game that is NEVER
// executed. Unlike the small transaction tests this exercises the full payload.
int main(int argc,char **argv) {
    @autoreleasepool {
        if (argc!=2) return 2;
        NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
        NSURL *source=[NSURL fileURLWithPath:@(argv[1])];
        NSURL *root=at([NSURL fileURLWithPath:NSTemporaryDirectory()],
            [@"BO3 Setup fixture " stringByAppendingString:NSUUID.UUID.UUIDString]);
        NSURL *payload=at(root,@"payload");
        require([fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:&error],error);
        require([fm copyItemAtURL:source toURL:payload error:&error],error);
        NSDictionary *copiedManifest=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:at(payload,@"manifest.json")] options:0 error:&error];
        for (NSDictionary *entry in copiedManifest[@"files"]) {
            NSString *relative=entry[@"path"];
            if (![[BO3SetupEngine sha256:at(payload,relative)] isEqual:entry[@"sha256"]]) {
                fprintf(stderr,"Prepared payload mismatch: %s (source matches: %s)\n",relative.UTF8String,
                    [[BO3SetupEngine sha256:at(source,relative)] isEqual:entry[@"sha256"]] ? "yes" : "no");
                require(NO,nil);
            }
        }
        for (NSString *kind in @[@"fresh",@"legacy"]) {
            NSURL *game=at(root,[kind stringByAppendingString:@" library/CoDBlkOps3.app"]);
            NSURL *executable=at(game,@"Contents/MacOS/CoDBlkOps3_Exe");
            NSURL *fix=at(game.URLByDeletingLastPathComponent,@"BO3MacFix");
            writeFixture(executable,@"Synthetic game fixture, never executed.");
            require(chmod(executable.fileSystemRepresentation,0755)==0,nil);
            writeFixture(at(game,@"Contents/MacOS/AppBundleExe"),@"original game launcher");
            if ([kind isEqual:@"legacy"]) {
                writeFixture(at(fix,@"BO3MacFix.dylib"),@"legacy patch");
                writeFixture(at(fix,@"AppBundleExe"),@"legacy launcher");
                writeFixture(at(fix,@"settings.ini"),@"owner settings");
                writeFixture(at(fix,@"pfx/save"),@"owner save");
            }
            NSData *data=[NSData dataWithContentsOfURL:at(payload,@"manifest.json")];
            NSMutableDictionary *manifest=[[NSJSONSerialization JSONObjectWithData:data options:0 error:&error] mutableCopy];
            require(manifest!=nil,error);
            manifest[@"game_sha256"]=@[[BO3SetupEngine sha256:executable]];
            data=[NSJSONSerialization dataWithJSONObject:manifest options:0 error:&error];
            require([data writeToURL:at(payload,@"manifest.json") options:NSDataWritingAtomic error:&error],error);
            require([BO3SetupEngine install:game payload:payload error:&error],error);
            require([BO3SetupEngine verifyInstalled:game payload:payload error:&error],error);
            require([BO3SetupEngine install:game payload:payload error:&error],error);
            require([BO3SetupEngine verifyInstalled:game payload:payload error:&error],error);
            require([fm removeItemAtURL:at(fix,@"AppBundleExe") error:&error],error);
            require([BO3SetupEngine install:game payload:payload error:&error],error);
            require([BO3SetupEngine verifyInstalled:game payload:payload error:&error],error);
            require([BO3SetupEngine remove:game error:&error],error);
            if ([kind isEqual:@"legacy"]) {
                require([[NSString stringWithContentsOfURL:at(fix,@"BO3MacFix.dylib") encoding:NSUTF8StringEncoding error:&error] isEqual:@"legacy patch"],error);
                require([[NSString stringWithContentsOfURL:at(fix,@"settings.ini") encoding:NSUTF8StringEncoding error:&error] isEqual:@"owner settings"],error);
                require([[NSString stringWithContentsOfURL:at(fix,@"pfx/save") encoding:NSUTF8StringEncoding error:&error] isEqual:@"owner save"],error);
            }
            printf("Full payload %s install, repeat update, repair and removal passed\n",kind.UTF8String);
        }
        require([fm removeItemAtURL:root error:&error],error);
    }
    return 0;
}
