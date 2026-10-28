#import "SetupEngine.h"
#import <AppKit/AppKit.h>
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static NSFileManager *fm(void) { return NSFileManager.defaultManager; }
static NSURL *child(NSURL *root, NSString *path) { return [root URLByAppendingPathComponent:path]; }
static NSURL *fixRoot(NSURL *game) { return child(game.URLByDeletingLastPathComponent,@"BO3MacFix"); }
static NSURL *pending(NSURL *root) { return child(root,@".setup-pending"); }
static BOOL failure(NSError **error, NSString *message) {
    if (error) *error=[NSError errorWithDomain:@"BO3MacFix Setup" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
static NSDictionary *json(NSURL *file) {
    NSData *data=[NSData dataWithContentsOfURL:file];
    id value=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    return [value isKindOfClass:NSDictionary.class] ? value : nil;
}
static BOOL writeJSON(NSDictionary *value, NSURL *file, NSError **error) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingPrettyPrinted error:error];
    if (!data || ![data writeToURL:file options:NSDataWritingAtomic error:error]) return NO;
    int fd=open(file.fileSystemRepresentation,O_RDONLY);
    BOOL ok=fd>=0 && fsync(fd)==0; if (fd>=0) close(fd);
    return ok;
}
static NSString *vdfValue(NSString *text, NSString *key) {
    NSString *pattern=[NSString stringWithFormat:@"\"%@\"\\s+\"((?:\\\\.|[^\"])*)\"", key];
    NSRegularExpression *regex=[NSRegularExpression regularExpressionWithPattern:pattern options:0 error:nil];
    NSTextCheckingResult *match=[regex firstMatchInString:text ?: @"" options:0 range:NSMakeRange(0,text.length)];
    if (!match) return nil;
    return [[[text substringWithRange:[match rangeAtIndex:1]] stringByReplacingOccurrencesOfString:@"\\\\" withString:@"\\"]
        stringByReplacingOccurrencesOfString:@"\\\"" withString:@"\""];
}
static BOOL safeRelative(NSString *path) {
    if (![path isKindOfClass:NSString.class] || !path.length || path.isAbsolutePath || [path containsString:@"\\"])
        return NO;
    for (NSString *part in path.pathComponents)
        if ([part isEqual:@".."] || [part isEqual:@"."] || !part.length) return NO;
    return YES;
}
static BOOL payloadPath(NSString *path) {
    if (!safeRelative(path)) return NO;
    NSString *first=path.pathComponents.firstObject;
    return ![first hasPrefix:@".setup-"] && ![@[@"pfx",@"Physics",@"settings.ini",@"setup-receipt.json",@"journal.json"] containsObject:first];
}
static BOOL noLinks(NSURL *root, NSString *relative, NSError **error) {
    NSURL *current=root;
    for (NSString *part in [@[@""] arrayByAddingObjectsFromArray:relative.pathComponents]) {
        if (part.length) current=child(current,part);
        struct stat info;
        if (lstat(current.fileSystemRepresentation,&info)==0 && S_ISLNK(info.st_mode))
            return failure(error,@"A setup destination is a symbolic link. Choose the actual game folder instead.");
    }
    return YES;
}
static BOOL makeParent(NSURL *file, NSError **error) {
    return [fm() createDirectoryAtURL:file.URLByDeletingLastPathComponent withIntermediateDirectories:YES
        attributes:nil error:error];
}
static BOOL digestString(id value) {
    if (![value isKindOfClass:NSString.class] || [value length]!=64) return NO;
    NSCharacterSet *invalid=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet];
    return [value rangeOfCharacterFromSet:invalid].location==NSNotFound;
}
static BOOL receiptValid(NSDictionary *record, NSError **error) {
    if (![record isKindOfClass:NSDictionary.class] || ![record[@"schema"] isEqual:@1] || ![record[@"baseline"] isKindOfClass:NSDictionary.class] ||
        ![record[@"installed"] isKindOfClass:NSDictionary.class] ||
        ![[NSSet setWithArray:[record[@"baseline"] allKeys]] isEqual:[NSSet setWithArray:[record[@"installed"] allKeys]]])
        return failure(error,@"The previous setup record is damaged. Nothing was changed; use Save Report for help.");
    for (NSString *path in record[@"installed"]) {
        id before=record[@"baseline"][path];
        if (!payloadPath(path) || !digestString(record[@"installed"][path]) ||
            (!digestString(before) && ![before isEqual:@"absent"]))
            return failure(error,@"The previous setup record is damaged. Nothing was changed; use Save Report for help.");
    }
    return YES;
}
#ifdef BO3SETUP_TESTING
static unsigned fail_copy;
void BO3SetupTestFailCopy(unsigned count) { fail_copy=count; }
#endif
static BOOL atomicCopy(NSURL *source, NSURL *destination, NSError **error) {
#ifdef BO3SETUP_TESTING
    if (fail_copy && --fail_copy==0) return failure(error,@"Injected copy failure for rollback verification");
#endif
    if (!makeParent(destination,error)) return NO;
    NSURL *stage=child(destination.URLByDeletingLastPathComponent,
        [@".bo3macfix-copy-" stringByAppendingString:NSUUID.UUID.UUIDString]);
    if (![fm() copyItemAtURL:source toURL:stage error:error]) return NO;
    int fd=open(stage.fileSystemRepresentation,O_RDONLY);
    BOOL synced=fd>=0 && fsync(fd)==0; if (fd>=0) close(fd);
    if (!synced) {
        [fm() removeItemAtURL:stage error:nil];
        return failure(error,@"Setup could not save a recovery copy. Check that the drive is writable.");
    }
    if (rename(stage.fileSystemRepresentation,destination.fileSystemRepresentation)!=0) {
        [fm() removeItemAtURL:stage error:nil];
        return failure(error,@"Could not replace a setup file. Check that the game is closed and the drive is writable.");
    }
    return YES;
}

@implementation BO3SetupEngine
+ (NSURL *)gameBundleAt:(NSURL *)selection {
    if (!selection) return nil;
    NSURL *root=selection.URLByResolvingSymlinksInPath;
    NSMutableArray *choices=[NSMutableArray arrayWithObjects:root,child(root,@"CoDBlkOps3.app"),nil];
    for (NSURL *entry in [fm() contentsOfDirectoryAtURL:root includingPropertiesForKeys:nil options:0 error:nil])
        if ([entry.pathExtension isEqual:@"app"]) [choices addObject:entry];
    for (NSURL *bundle in choices) {
        if ([fm() isExecutableFileAtPath:child(bundle,@"Contents/MacOS/CoDBlkOps3_Exe").path]) return bundle;
    }
    return nil;
}
+ (NSArray<NSURL *> *)findGames {
    NSURL *steam=child([NSURL fileURLWithPath:NSHomeDirectory()],@"Library/Application Support/Steam");
    NSMutableOrderedSet<NSURL *> *libraries=[NSMutableOrderedSet orderedSetWithObject:steam];
    NSString *vdf=[NSString stringWithContentsOfURL:child(steam,@"steamapps/libraryfolders.vdf")
        encoding:NSUTF8StringEncoding error:nil];
    NSRegularExpression *regex=[NSRegularExpression regularExpressionWithPattern:@"\"path\"\\s+\"((?:\\\\.|[^\"])*)\""
        options:0 error:nil];
    for (NSTextCheckingResult *match in [regex matchesInString:vdf ?: @"" options:0 range:NSMakeRange(0,vdf.length)]) {
        NSString *path=[[vdf substringWithRange:[match rangeAtIndex:1]] stringByReplacingOccurrencesOfString:@"\\\\" withString:@"\\"];
        [libraries addObject:[NSURL fileURLWithPath:path]];
    }
    NSMutableOrderedSet *games=[NSMutableOrderedSet orderedSet];
    for (NSURL *library in libraries) {
        NSString *manifest=[NSString stringWithContentsOfURL:child(library,@"steamapps/appmanifest_311210.acf")
            encoding:NSUTF8StringEncoding error:nil];
        NSString *directory=vdfValue(manifest,@"installdir");
        if (!safeRelative(directory)) continue;
        NSURL *game=[self gameBundleAt:child(child(library,@"steamapps/common"),directory)];
        if (game) [games addObject:game];
    }
    return games.array;
}
+ (NSString *)sha256:(NSURL *)file {
    struct stat info;
    if (lstat(file.fileSystemRepresentation,&info)!=0 || !S_ISREG(info.st_mode)) return nil;
    NSInputStream *stream=[NSInputStream inputStreamWithURL:file]; [stream open];
    CC_SHA256_CTX ctx; CC_SHA256_Init(&ctx); uint8_t buffer[65536]; NSInteger count;
    while ((count=[stream read:buffer maxLength:sizeof(buffer)])>0) CC_SHA256_Update(&ctx,buffer,(CC_LONG)count);
    NSError *error=stream.streamError; [stream close]; if (count<0 || error) return nil;
    uint8_t digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_Final(digest,&ctx);
    NSMutableString *text=[NSMutableString string];
    for (size_t i=0;i<sizeof(digest);++i) [text appendFormat:@"%02x",digest[i]];
    return text;
}
+ (BOOL)gameIsRunning {
    for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) {
        NSString *name=app.executableURL.lastPathComponent;
        if ([name isEqual:@"CoDBlkOps3_Exe"] || [name isEqual:@"CoDBlkOps3_Exe.bo3macfix"]) return YES;
    }
    return NO;
}
+ (NSDictionary *)readiness:(NSURL *)game payload:(NSURL *)payload error:(NSError **)error {
    NSDictionary *manifest=json(child(payload,@"manifest.json"));
    if (!manifest || [manifest[@"schema"] integerValue]!=1 ||
        (![manifest[@"ready_for_coop"] boolValue] && ![manifest[@"test_candidate"] isEqual:@YES])) {
        failure(error,@"This installer is still being prepared. Please use a newer build before testing AAE or UEM."); return nil;
    }
    NSURL *valid=[self gameBundleAt:game];
    if (!valid) { failure(error,@"Black Ops III was not found. Use Choose Game Folder."); return nil; }
    NSString *hash=[self sha256:child(valid,@"Contents/MacOS/CoDBlkOps3_Exe")];
    if (![manifest[@"game_sha256"] isKindOfClass:NSArray.class] || ![manifest[@"game_sha256"] containsObject:hash ?: @""]) {
        failure(error,@"This game version is not supported by this installer. Your game has not been changed."); return nil;
    }
    NSArray *files=manifest[@"files"];
    if (![files isKindOfClass:NSArray.class] || files.count==0) {
        failure(error,@"The installer is incomplete. Download it again."); return nil;
    }
    NSMutableSet *seen=[NSMutableSet set];
    for (NSDictionary *entry in files) {
        if (![entry isKindOfClass:NSDictionary.class]) {
            failure(error,@"The installer file list is damaged. Download it again."); return nil;
        }
        NSString *path=entry[@"path"];
        if (!payloadPath(path) || [seen containsObject:path] ||
            !noLinks(payload,path,error) || !noLinks(fixRoot(valid),path,error) ||
            ![[self sha256:child(payload,path)] isEqual:entry[@"sha256"]]) {
            failure(error,@"An installation file is missing or changed. Download the installer again."); return nil;
        }
        [seen addObject:path];
    }
    if (![seen containsObject:@"BO3MacFix.dylib"] || ![seen containsObject:@"AppBundleExe"]) {
        failure(error,@"The installer is missing its game launcher or patch."); return nil;
    }
    if ([manifest[@"requires_bundled_wine"] boolValue] &&
        ![seen containsObject:@"runtime/wine/bin/wine"] && ![seen containsObject:@"runtime/wine/bin/wine64"]) {
        failure(error,@"The installer is missing a required helper. Download the complete installer."); return nil;
    }
    return manifest;
}
+ (BOOL)isInstalled:(NSURL *)game { return json(child(fixRoot(game),@"setup-receipt.json"))!=nil; }
+ (BOOL)needsRecovery:(NSURL *)game { return [fm() fileExistsAtPath:pending(fixRoot(game)).path]; }
+ (BOOL)recover:(NSURL *)game error:(NSError **)error {
    if (![self needsRecovery:game]) return YES;
    if ([self gameIsRunning]) return failure(error,@"Close Black Ops III, then click Repair again.");
    NSURL *root=fixRoot(game), *transaction=pending(root);
    if (!noLinks(root,@".setup-pending/journal.json",error) || !noLinks(root,@"setup-receipt.json",error)) return NO;
    NSDictionary *record=json(child(transaction,@"journal.json"));
    NSDictionary *before=record[@"before"], *after=record[@"after"];
    id previous=record[@"previous_receipt"], final=record[@"final_receipt"];
    NSArray *newBackups=record[@"new_backups"], *cleanup=record[@"cleanup_backups"];
    BOOL removal=[record[@"kind"] isEqual:@"remove"];
    if (![record[@"schema"] isEqual:@1] || (!removal && ![record[@"kind"] isEqual:@"install"]) ||
        ![before isKindOfClass:NSDictionary.class] || ![after isKindOfClass:NSDictionary.class] ||
        ![[NSSet setWithArray:before.allKeys] isEqual:[NSSet setWithArray:after.allKeys]] ||
        ![newBackups isKindOfClass:NSArray.class] || ![cleanup isKindOfClass:NSArray.class] ||
        (![previous isEqual:NSNull.null] && !receiptValid(previous,nil)) ||
        (![final isEqual:NSNull.null] && !receiptValid(final,nil)))
        return failure(error,@"The saved recovery record needs review. Nothing was changed; use Save Report for help.");
    NSDictionary *current=json(child(root,@"setup-receipt.json"));
    BOOL receiptExists=[fm() fileExistsAtPath:child(root,@"setup-receipt.json").path];
    BOOL isBefore=[previous isEqual:NSNull.null] ? !receiptExists : [current isEqual:previous];
    BOOL isAfter=[final isEqual:NSNull.null] ? !receiptExists : [current isEqual:final];
    if (!isBefore && !isAfter) return failure(error,@"The setup record changed after an interruption. Nothing was changed; use Save Report for help.");
    BOOL complete=isAfter;
    for (NSString *path in before) {
        if (!payloadPath(path) || (!digestString(before[path]) && ![before[path] isEqual:@"absent"]) ||
            (!digestString(after[path]) && ![after[path] isEqual:@"absent"]) ||
            !noLinks(root,path,error) || !noLinks(transaction,path,error))
            return failure(error,@"The recovery file list needs review. Nothing was changed.");
        NSString *now=[self sha256:child(root,path)] ?: ([fm() fileExistsAtPath:child(root,path).path] ? @"invalid" : @"absent");
        if (![now isEqual:before[path]] && ![now isEqual:after[path]])
            return failure(error,@"A file changed after setup was interrupted. It has been kept; use Save Report for help.");
        if (![now isEqual:after[path]]) complete=NO;
        if (![before[path] isEqual:@"absent"] && ![[self sha256:child(transaction,path)] isEqual:before[path]])
            return failure(error,@"A recovery copy is missing or changed. Nothing was changed.");
    }
    /* Validate every backup cleanup path before any recovery write. */
    for (NSString *path in [newBackups arrayByAddingObjectsFromArray:cleanup]) {
        if (!safeRelative(path) || !before[path] || !noLinks(root,[@".setup-backup/" stringByAppendingString:path],error))
            return failure(error,@"The recovery backup list needs review. Nothing was changed.");
        NSURL *backup=child(child(root,@".setup-backup"),path);
        if ([fm() fileExistsAtPath:backup.path] &&
            ![[self sha256:backup] isEqual:(removal ? after[path] : before[path])])
            return failure(error,@"A backup changed after an interruption. It has been kept; use Save Report for help.");
    }
    if (!complete) {
        for (NSString *path in before) {
            NSURL *destination=child(root,path);
            if ([before[path] isEqual:@"absent"]) {
                if ([fm() fileExistsAtPath:destination.path] && ![fm() removeItemAtURL:destination error:error]) return NO;
            } else if (!atomicCopy(child(transaction,path),destination,error)) return NO;
        }
        if (![previous isEqual:NSNull.null]) {
            if (!writeJSON(previous,child(root,@"setup-receipt.json"),error)) return NO;
        } else if (receiptExists && ![fm() removeItemAtURL:child(root,@"setup-receipt.json") error:error]) return NO;
    }
    NSArray *discard=complete ? cleanup : newBackups;
    for (NSString *path in discard) {
        NSURL *backup=child(child(root,@".setup-backup"),path);
        if ([fm() fileExistsAtPath:backup.path] && ![fm() removeItemAtURL:backup error:error]) return NO;
    }
    return [fm() removeItemAtURL:transaction error:error];
}
+ (BOOL)verifyInstalled:(NSURL *)game payload:(NSURL *)payload error:(NSError **)error {
    if ([self needsRecovery:game]) return failure(error,@"Setup was interrupted. Click Repair to finish recovering your files.");
    NSDictionary *manifest=[self readiness:game payload:payload error:error];
    if (!manifest || ![self isInstalled:game]) return NO;
    NSURL *root=fixRoot(game);
    if (!noLinks(root,@"setup-receipt.json",error)) return NO;
    NSDictionary *receipt=json(child(root,@"setup-receipt.json"));
    if (!receiptValid(receipt,error)) return NO;
    if (manifest[@"dependencies_id"] && ![manifest[@"dependencies_id"] isEqual:receipt[@"dependencies_id"]])
        return failure(error,@"This installation needs its helpers. Click Update before playing.");
    for (NSString *path in receipt[@"installed"])
        if (!noLinks(root,path,error) || ![[self sha256:child(root,path)] isEqual:receipt[@"installed"][path]])
            return failure(error,@"A helper is missing or changed. Click Update to repair it before playing.");
    for (NSDictionary *entry in manifest[@"files"]) {
        NSString *path=entry[@"path"];
        if (!noLinks(fixRoot(game),path,error) || ![[self sha256:child(fixRoot(game),path)] isEqual:entry[@"sha256"]])
            return failure(error,@"This installation needs an update or repair. Click Update before playing.");
    }
    return YES;
}
// Stage all recovery copies and the complete plan before replacing a game file.
+ (BOOL)install:(NSURL *)game payload:(NSURL *)payload error:(NSError **)error {
    if ([self gameIsRunning]) return failure(error,@"Close Black Ops III, then click Install again.");
    NSDictionary *manifest=[self readiness:game payload:payload error:error]; if (!manifest) return NO;
    if (manifest[@"dependencies_id"] && ![manifest[@"dependencies_prepared"] isEqual:@YES])
        return failure(error,@"Setup must finish downloading its helpers before installation.");
    if (![self recover:game error:error]) return NO;
    NSURL *root=fixRoot(game);
    if (!noLinks(root,@"setup-receipt.json",error) || !noLinks(root,@".setup-backup",error)) return NO;
    if (![fm() createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:error]) return NO;
    NSDictionary *previous=json(child(root,@"setup-receipt.json"));
    if ([fm() fileExistsAtPath:child(root,@"setup-receipt.json").path] && !receiptValid(previous,error)) return NO;
    NSMutableDictionary *baseline=[previous[@"baseline"] mutableCopy] ?: [NSMutableDictionary dictionary];
    NSMutableDictionary *installed=[previous[@"installed"] mutableCopy] ?: [NSMutableDictionary dictionary];
    NSMutableSet *replacements=[NSMutableSet set];
    for (NSDictionary *entry in manifest[@"files"]) [replacements addObject:entry[@"path"]];
    for (NSString *path in installed) {
        if (!noLinks(root,path,error)) return NO;
        BOOL missing=![fm() fileExistsAtPath:child(root,path).path];
        // Update can restore a deleted helper. Existing edits and recovery
        // backups are still preserved; a missing file must have a replacement.
        if (missing ? ![replacements containsObject:path] : ![[self sha256:child(root,path)] isEqual:installed[path]])
            return failure(error,@"A file from the previous installation has changed. Nothing was replaced; use Save Report for help.");
        NSString *before=baseline[path];
        if (![before isEqual:@"absent"] && (!noLinks(root,[@".setup-backup/" stringByAppendingString:path],error) ||
            ![[self sha256:child(child(root,@".setup-backup"),path)] isEqual:before]))
            return failure(error,@"An original backup is missing or changed. Nothing was replaced.");
    }
    NSURL *stage=child(root,[@".setup-staging-" stringByAppendingString:NSUUID.UUID.UUIDString]);
    if (![fm() createDirectoryAtURL:stage withIntermediateDirectories:NO attributes:nil error:error]) return NO;
    NSMutableDictionary *before=[NSMutableDictionary dictionary], *after=[NSMutableDictionary dictionary];
    NSMutableArray *newBackups=[NSMutableArray array]; BOOL staged=YES;
    for (NSDictionary *entry in manifest[@"files"]) {
        NSString *path=entry[@"path"]; NSURL *destination=child(root,path);
        BOOL exists=[fm() fileExistsAtPath:destination.path];
        NSString *hash=exists ? [self sha256:destination] : @"absent";
        if (!hash || !noLinks(root,path,error)) { staged=NO; break; }
        before[path]=hash; after[path]=entry[@"sha256"];
        if (exists && (!atomicCopy(destination,child(stage,path),error) ||
            ![[self sha256:child(stage,path)] isEqual:hash])) { staged=NO; break; }
        if (!baseline[path]) {
            baseline[path]=hash;
            if (exists) {
                NSURL *backup=child(child(root,@".setup-backup"),path);
                if ([fm() fileExistsAtPath:backup.path] || !noLinks(root,[@".setup-backup/" stringByAppendingString:path],error)) {
                    failure(error,@"An existing backup needs review. Nothing was replaced; use Save Report for help."); staged=NO; break;
                }
                [newBackups addObject:path];
            }
        }
        installed[path]=entry[@"sha256"];
    }
    NSDictionary *receipt=@{@"schema":@1,@"baseline":baseline,@"installed":installed,
        @"version":manifest[@"version"] ?: @"unknown",@"dependencies_id":manifest[@"dependencies_id"] ?: @"none",
        @"helper_arch":manifest[@"helper_arch"] ?: @"unknown"};
    NSDictionary *journal=@{@"schema":@1,@"kind":@"install",@"before":before,@"after":after,
        @"previous_receipt":previous ?: (id)NSNull.null,@"final_receipt":receipt,
        @"new_backups":newBackups,@"cleanup_backups":@[]};
    if (!staged || !writeJSON(journal,child(stage,@"journal.json"),error) ||
        rename(stage.fileSystemRepresentation,pending(root).fileSystemRepresentation)!=0) {
        [fm() removeItemAtURL:stage error:nil];
        return failure(error,@"Setup could not prepare its recovery files. Your previous installation was kept.");
    }
    NSURL *transaction=pending(root); BOOL ok=YES;
    for (NSString *path in newBackups) {
        NSURL *backup=child(child(root,@".setup-backup"),path);
        if (!atomicCopy(child(transaction,path),backup,error) || ![[self sha256:backup] isEqual:before[path]]) { ok=NO;break; }
    }
    if (ok) for (NSDictionary *entry in manifest[@"files"]) {
        NSString *path=entry[@"path"]; NSURL *destination=child(root,path);
        NSString *current=[self sha256:destination] ?: ([fm() fileExistsAtPath:destination.path] ? @"invalid" : @"absent");
        if (!noLinks(root,path,error) || ![current isEqual:before[path]] ||
            !atomicCopy(child(payload,path),destination,error) ||
            ([path isEqual:@"AppBundleExe"] && chmod(destination.fileSystemRepresentation,0755)!=0) ||
            ![[self sha256:destination] isEqual:entry[@"sha256"]]) { ok=NO;break; }
    }
    if (ok) ok=writeJSON(receipt,child(root,@"setup-receipt.json"),error);
    if (!ok) {
        if (![self recover:game error:nil]) return failure(error,@"Setup was interrupted. Click Repair to restore your previous files.");
        return failure(error,@"Setup could not confirm completion. Recovery finished; check the app's status before trying again.");
    }
    return [self recover:game error:error]; // Committed receipt: verify and finish cleanup.
}
+ (BOOL)remove:(NSURL *)game error:(NSError **)error {
    if ([self gameIsRunning]) return failure(error,@"Close Black Ops III, then click Remove again.");
    if (![self recover:game error:error]) return NO;
    NSURL *root=fixRoot(game);
    if (!noLinks(root,@"setup-receipt.json",error) || !noLinks(root,@".setup-backup",error)) return NO;
    NSDictionary *receipt=json(child(root,@"setup-receipt.json"));
    if (!receipt) return failure(error,@"This app has no installation to remove.");
    if (!receiptValid(receipt,error)) return NO;
    NSDictionary *installed=receipt[@"installed"], *baseline=receipt[@"baseline"];
    for (NSString *path in installed) {
        if (!noLinks(root,path,error) || ![[self sha256:child(root,path)] isEqual:installed[path]])
            return failure(error,@"An installed file has changed since setup. Nothing was removed; use Save Report for help.");
        NSString *before=baseline[path];
        if (![before isEqual:@"absent"] && (!noLinks(root,[@".setup-backup/" stringByAppendingString:path],error) ||
            ![[self sha256:child(child(root,@".setup-backup"),path)] isEqual:before]))
            return failure(error,@"An original backup is missing or changed. Nothing was removed.");
    }
    NSURL *stage=child(root,[@".setup-staging-" stringByAppendingString:NSUUID.UUID.UUIDString]);
    if (![fm() createDirectoryAtURL:stage withIntermediateDirectories:NO attributes:nil error:error]) return NO;
    NSMutableArray *cleanup=[NSMutableArray array]; BOOL staged=YES;
    for (NSString *path in installed) {
        if (!atomicCopy(child(root,path),child(stage,path),error) ||
            ![[self sha256:child(stage,path)] isEqual:installed[path]]) { staged=NO; break; }
        if (![baseline[path] isEqual:@"absent"]) [cleanup addObject:path];
    }
    NSDictionary *journal=@{@"schema":@1,@"kind":@"remove",@"before":installed,@"after":baseline,
        @"previous_receipt":receipt,@"final_receipt":NSNull.null,@"new_backups":@[],@"cleanup_backups":cleanup};
    if (!staged || !writeJSON(journal,child(stage,@"journal.json"),error) ||
        rename(stage.fileSystemRepresentation,pending(root).fileSystemRepresentation)!=0) {
        [fm() removeItemAtURL:stage error:nil];
        return failure(error,@"Removal could not prepare its recovery files. Nothing was removed.");
    }
    BOOL ok=YES;
    for (NSString *path in installed) {
        if ([baseline[path] isEqual:@"absent"]) ok=[fm() removeItemAtURL:child(root,path) error:error];
        else ok=atomicCopy(child(child(root,@".setup-backup"),path),child(root,path),error);
        if (!ok) break;
    }
    if (ok) ok=[fm() removeItemAtURL:child(root,@"setup-receipt.json") error:error];
    if (!ok) {
        if (![self recover:game error:nil]) return failure(error,@"Removal was interrupted. Click Repair to recover your installation.");
        return failure(error,@"Removal could not finish. Your installation was restored; please try again.");
    }
    return [self recover:game error:error];
}
+ (NSDictionary *)diagnostics:(NSURL *)game payload:(NSURL *)payload {
    // Deliberately omit hardware IDs, Steam account IDs, network URLs and logs.
    NSDictionary *manifest=json(child(payload,@"manifest.json")) ?: @{};
    NSString *hash=game ? [self sha256:child(game,@"Contents/MacOS/CoDBlkOps3_Exe")] : nil;
    return @{@"schema":@1,@"macOS":NSProcessInfo.processInfo.operatingSystemVersionString,
        @"game_found":@(game!=nil),@"game_sha256":hash ?: @"unknown",
        @"installed":@(game && [self isInstalled:game]),@"payload_version":manifest[@"version"] ?: @"development",
        @"recovery_pending":@(game && [self needsRecovery:game]),
        @"ready_for_coop":manifest[@"ready_for_coop"] ?: @NO};
}
@end
