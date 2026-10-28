#import <Foundation/Foundation.h>
#import "../installer/SetupEngine.h"
#import "../installer/SetupDependencies.h"
#include <assert.h>
#include <sys/stat.h>
void BO3SetupTestFailCopy(unsigned count);
static NSURL *child(NSURL *root,NSString *name) { return [root URLByAppendingPathComponent:name]; }
static void writeFile(NSURL *file,NSString *text) {
    assert([NSFileManager.defaultManager createDirectoryAtURL:file.URLByDeletingLastPathComponent
        withIntermediateDirectories:YES attributes:nil error:nil]);
    assert([text writeToURL:file atomically:YES encoding:NSUTF8StringEncoding error:nil]);
}
static NSString *readFile(NSURL *file) { return [NSString stringWithContentsOfURL:file encoding:NSUTF8StringEncoding error:nil]; }
static void writeJSON(NSURL *file,NSDictionary *value) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:value options:0 error:nil];
    assert([data writeToURL:file options:NSDataWritingAtomic error:nil]);
}
static void recoveryFixtures(NSURL *game,NSURL *payload,NSURL *fix) {
    NSFileManager *fm=NSFileManager.defaultManager; NSError *error=nil;
    NSString *patch=@"BO3MacFix.dylib", *helper=@"AppBundleExe";
    NSURL *pending=child(fix,@".setup-pending"), *backup=child(fix,@".setup-backup/BO3MacFix.dylib");
    writeFile(child(payload,patch),@"candidate patch");writeFile(child(payload,helper),@"candidate helper");
    writeFile(child(fix,patch),@"old patch");
    NSString *old=[BO3SetupEngine sha256:child(fix,patch)];
    NSDictionary *before=@{patch:old,helper:@"absent"};
    NSDictionary *after=@{patch:[BO3SetupEngine sha256:child(payload,patch)],helper:[BO3SetupEngine sha256:child(payload,helper)]};
    NSDictionary *receipt=@{@"schema":@1,@"baseline":before,@"installed":after,@"version":@"fixture-only"};
    NSMutableDictionary *journal=[@{@"schema":@1,@"kind":@"install",@"before":before,@"after":after,
        @"previous_receipt":NSNull.null,@"final_receipt":receipt,@"new_backups":@[patch],@"cleanup_backups":@[]} mutableCopy];
    // Interrupted fresh installation: one new file is committed, no receipt.
    writeFile(child(pending,patch),@"old patch");writeFile(backup,@"old patch");
    writeFile(child(fix,patch),@"candidate patch");writeJSON(child(pending,@"journal.json"),journal);
    assert([BO3SetupEngine needsRecovery:game]);
    assert(![BO3SetupEngine verifyInstalled:game payload:payload error:&error]);
    assert([BO3SetupEngine recover:game error:&error]);
    assert([readFile(child(fix,patch)) isEqual:@"old patch"] && ![fm fileExistsAtPath:backup.path]);
    assert(![BO3SetupEngine needsRecovery:game] && ![BO3SetupEngine isInstalled:game]);
    // After an atomic receipt commit, reopening completes cleanup instead of
    // undoing the successfully installed version.
    writeFile(child(pending,patch),@"old patch");writeFile(backup,@"old patch");
    writeFile(child(fix,patch),@"candidate patch");writeFile(child(fix,helper),@"candidate helper");
    writeJSON(child(pending,@"journal.json"),journal);writeJSON(child(fix,@"setup-receipt.json"),receipt);
    assert([BO3SetupEngine recover:game error:&error] && [BO3SetupEngine isInstalled:game]);
    assert([readFile(child(fix,patch)) isEqual:@"candidate patch"] && [fm fileExistsAtPath:backup.path]);
    // Interrupted removal retains its receipt and restores the complete install.
    journal=[@{@"schema":@1,@"kind":@"remove",@"before":after,@"after":before,
        @"previous_receipt":receipt,@"final_receipt":NSNull.null,@"new_backups":@[],@"cleanup_backups":@[patch]} mutableCopy];
    writeFile(child(pending,patch),@"candidate patch");writeFile(child(pending,helper),@"candidate helper");
    writeFile(child(fix,patch),@"old patch");assert([fm removeItemAtURL:child(fix,helper) error:nil]);
    writeJSON(child(pending,@"journal.json"),journal);
    assert([BO3SetupEngine recover:game error:&error]);
    assert([readFile(child(fix,helper)) isEqual:@"candidate helper"] && [fm fileExistsAtPath:backup.path]);
    // Owner edits after an interruption are preserved, and every file is
    // checked before the first recovery write.
    writeFile(child(pending,patch),@"candidate patch");writeFile(child(pending,helper),@"candidate helper");
    writeFile(child(fix,patch),@"owner change");writeJSON(child(pending,@"journal.json"),journal);
    assert(![BO3SetupEngine recover:game error:&error]);
    assert([readFile(child(fix,patch)) isEqual:@"owner change"]);
    writeFile(child(fix,patch),@"old patch");assert([fm removeItemAtURL:child(fix,helper) error:nil]);
    assert([fm removeItemAtURL:child(fix,@"setup-receipt.json") error:nil]);
    // Completed removal, interrupted during cleanup: keep the restored baseline.
    assert([BO3SetupEngine recover:game error:&error]);
    assert([readFile(child(fix,patch)) isEqual:@"old patch"] && ![fm fileExistsAtPath:backup.path]);
    assert(![BO3SetupEngine needsRecovery:game] && ![BO3SetupEngine isInstalled:game]);
    assert([readFile(child(fix,@"pfx/saved-data")) isEqual:@"keep this"]);
}
static void manifest(NSURL *payload,NSURL *game) {
    NSMutableArray *files=[NSMutableArray array];
    for (NSString *name in @[@"BO3MacFix.dylib",@"AppBundleExe"])
        [files addObject:@{@"path":name,@"sha256":[BO3SetupEngine sha256:child(payload,name)]}];
    NSDictionary *value=@{@"schema":@1,@"ready_for_coop":@YES,@"version":@"fixture-only",
        @"game_sha256":@[[BO3SetupEngine sha256:child(game,@"Contents/MacOS/CoDBlkOps3_Exe")]],@"files":files};
    NSData *json=[NSJSONSerialization dataWithJSONObject:value options:0 error:nil];
    assert([json writeToURL:child(payload,@"manifest.json") atomically:YES]);
}
int main(void) {
    @autoreleasepool {
        NSURL *root=child([NSURL fileURLWithPath:NSTemporaryDirectory()],NSUUID.UUID.UUIDString);
        NSURL *game=child(root,@"library/Game.app"), *payload=child(root,@"payload");
        NSURL *fix=child(game.URLByDeletingLastPathComponent,@"BO3MacFix");
        // Synthetic files are never executed. The matching fixture hash is
        // intentionally separate from the empty production Mac profile list.
        writeFile(child(game,@"Contents/MacOS/CoDBlkOps3_Exe"),@"not executable code: installer fixture");
        assert(chmod(child(game,@"Contents/MacOS/CoDBlkOps3_Exe").fileSystemRepresentation,0755)==0);
        writeFile(child(game,@"Contents/MacOS/AppBundleExe"),@"original game launcher");
        writeFile(child(payload,@"BO3MacFix.dylib"),@"new patch");
        writeFile(child(payload,@"AppBundleExe"),@"new helper");
        writeFile(child(fix,@"BO3MacFix.dylib"),@"old patch");
        writeFile(child(fix,@"pfx/saved-data"),@"keep this");
        manifest(payload,game);
        NSError *error=nil;
        BO3SetupTestFailCopy(3);
        assert(![BO3SetupEngine install:game payload:payload error:&error]);
        assert([readFile(child(fix,@"BO3MacFix.dylib")) isEqual:@"old patch"]);
        assert(![BO3SetupEngine isInstalled:game]);
        assert([BO3SetupEngine install:game payload:payload error:&error]);
        assert([BO3SetupEngine isInstalled:game]);
        assert([readFile(child(fix,@"BO3MacFix.dylib")) isEqual:@"new patch"]);
        assert([readFile(child(game,@"Contents/MacOS/AppBundleExe")) isEqual:@"original game launcher"]);
        writeFile(child(payload,@"BO3MacFix.dylib"),@"updated patch");
        manifest(payload,game);
        assert([BO3SetupEngine install:game payload:payload error:&error]);
        // Update repairs a missing owned file, including rollback if its copy
        // fails. An edited owned file is still preserved rather than replaced.
        assert([NSFileManager.defaultManager removeItemAtURL:child(fix,@"AppBundleExe") error:nil]);
        assert(![BO3SetupEngine verifyInstalled:game payload:payload error:&error]);
        BO3SetupTestFailCopy(2);
        assert(![BO3SetupEngine install:game payload:payload error:&error]);
        assert(![NSFileManager.defaultManager fileExistsAtPath:child(fix,@"AppBundleExe").path]);
        assert([readFile(child(fix,@"BO3MacFix.dylib")) isEqual:@"updated patch"]);
        assert([BO3SetupEngine install:game payload:payload error:&error]);
        assert([BO3SetupEngine verifyInstalled:game payload:payload error:&error]);
        writeFile(child(fix,@"BO3MacFix.dylib"),@"owner edit");
        assert(![BO3SetupEngine install:game payload:payload error:&error]);
        assert(![BO3SetupEngine remove:game error:&error]);
        assert([readFile(child(fix,@"BO3MacFix.dylib")) isEqual:@"owner edit"]);
        writeFile(child(fix,@"BO3MacFix.dylib"),@"updated patch");
        assert([BO3SetupEngine remove:game error:&error]);
        assert([readFile(child(fix,@"BO3MacFix.dylib")) isEqual:@"old patch"]);
        assert([readFile(child(fix,@"pfx/saved-data")) isEqual:@"keep this"]);
        assert(![NSFileManager.defaultManager fileExistsAtPath:child(fix,@"AppBundleExe").path]);
        // Dereference internal vendor aliases into regular deployable files;
        // reject external aliases before copying their contents.
        NSURL *tree=child(root,@"vendor"), *copied=child(root,@"copied");
        writeFile(child(tree,@"lib/original"),@"helper");
        assert([NSFileManager.defaultManager createSymbolicLinkAtPath:child(tree,@"lib/alias").path withDestinationPath:@"original" error:nil]);
        assert([BO3SetupDependencies copyTree:tree to:copied boundary:tree error:&error]);
        struct stat info; assert(lstat(child(copied,@"lib/alias").fileSystemRepresentation,&info)==0 && S_ISREG(info.st_mode));
        assert([readFile(child(copied,@"lib/alias")) isEqual:@"helper"]);
        assert([NSFileManager.defaultManager createSymbolicLinkAtPath:child(tree,@"escape").path withDestinationPath:@"../payload/AppBundleExe" error:nil]);
        assert(![BO3SetupDependencies copyTree:tree to:child(root,@"refused") boundary:tree error:&error]);
        // The explicit first-test manifest is installable without pretending
        // that co-op has passed. Unprepared downloads cannot be installed.
        NSMutableDictionary *candidate=[[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:child(payload,@"manifest.json")] options:0 error:nil] mutableCopy];
        candidate[@"ready_for_coop"]=@NO; candidate[@"test_candidate"]=@YES; candidate[@"dependencies_id"]=@"fixture-deps";
        writeJSON(child(payload,@"manifest.json"),candidate);
        assert([BO3SetupEngine readiness:game payload:payload error:&error]);
        assert(![BO3SetupEngine install:game payload:payload error:&error]);
        candidate[@"dependencies_prepared"]=@YES;
        writeFile(child(payload,@"runtime/wine/bin/wine"),@"synthetic helper");
        NSMutableArray *candidateFiles=[candidate[@"files"] mutableCopy];
        [candidateFiles addObject:@{@"path":@"runtime/wine/bin/wine",@"sha256":[BO3SetupEngine sha256:child(payload,@"runtime/wine/bin/wine")]}];
        candidate[@"files"]=candidateFiles; candidate[@"requires_bundled_wine"]=@YES;
        writeJSON(child(payload,@"manifest.json"),candidate);
        assert([BO3SetupEngine install:game payload:payload error:&error]);
        // On the next app launch only the small base payload is present. The
        // receipt must still verify every downloaded helper before Play.
        candidate[@"files"]=[candidateFiles subarrayWithRange:NSMakeRange(0,2)]; candidate[@"requires_bundled_wine"]=@NO;
        writeJSON(child(payload,@"manifest.json"),candidate);
        assert([BO3SetupEngine verifyInstalled:game payload:payload error:&error]);
        writeFile(child(fix,@"runtime/wine/bin/wine"),@"changed helper");
        assert(![BO3SetupEngine verifyInstalled:game payload:payload error:&error]);
        assert([NSFileManager.defaultManager removeItemAtURL:child(fix,@"runtime/wine/bin/wine") error:nil]);
        // The small base payload cannot repair a helper it does not contain.
        assert(![BO3SetupEngine install:game payload:payload error:&error]);
        candidate[@"files"]=candidateFiles; candidate[@"requires_bundled_wine"]=@YES;
        writeJSON(child(payload,@"manifest.json"),candidate);
        assert([BO3SetupEngine install:game payload:payload error:&error]);
        assert([BO3SetupEngine verifyInstalled:game payload:payload error:&error]);
        assert([BO3SetupEngine remove:game error:&error]);
        manifest(payload,game);
        writeFile(child(payload,@"BO3MacFix.dylib"),@"corrupted payload");
        assert(![BO3SetupEngine install:game payload:payload error:&error]);
        assert([readFile(child(fix,@"BO3MacFix.dylib")) isEqual:@"old patch"]);
        recoveryFixtures(game,payload,fix);
        assert([NSFileManager.defaultManager removeItemAtURL:root error:nil]);
        puts("macOS installer rollback, update, interrupted-install/removal recovery and preservation tests passed");
    }
    return 0;
}
