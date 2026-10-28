#import "SetupDependencies.h"
#import "SetupEngine.h"
#include <sys/stat.h>
#include <sys/sysctl.h>

static NSString *hostArchitecture(void) {
#if defined(__aarch64__) || defined(__arm64__)
    return @"arm64";
#else
    int translated=0;size_t size=sizeof(translated);
    return sysctlbyname("sysctl.proc_translated",&translated,&size,NULL,0)==0 && translated ? @"arm64" : @"x86_64";
#endif
}

static BOOL fail(NSError **error, NSString *message) {
    if (error) *error=[NSError errorWithDomain:@"BO3MacFix Setup" code:2 userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
static NSURL *at(NSURL *root, NSString *path) { return [root URLByAppendingPathComponent:path]; }
static NSDictionary *readJSON(NSURL *file) {
    NSData *data=[NSData dataWithContentsOfURL:file];
    id value=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    return [value isKindOfClass:NSDictionary.class] ? value : nil;
}
static BOOL within(NSURL *file, NSURL *root) {
    NSString *path=file.URLByResolvingSymlinksInPath.path, *base=root.URLByResolvingSymlinksInPath.path;
    return [path isEqual:base] || [path hasPrefix:[base stringByAppendingString:@"/"]];
}
static BOOL copyTree(NSURL *source, NSURL *destination, NSURL *boundary, unsigned depth, NSError **error) {
    NSFileManager *fm=NSFileManager.defaultManager;
    if (depth>64 || !within(source,boundary)) return fail(error,@"A downloaded helper contains an unsafe file link. Nothing was installed.");
    source=source.URLByResolvingSymlinksInPath;
    struct stat info;
    if (lstat(source.fileSystemRepresentation,&info)!=0) return fail(error,@"A downloaded helper is incomplete. Please try Install again.");
    if (S_ISREG(info.st_mode)) {
        if (![fm createDirectoryAtURL:destination.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:error]) return NO;
        return [fm copyItemAtURL:source toURL:destination error:error];
    }
    if (!S_ISDIR(info.st_mode)) return fail(error,@"A downloaded helper contains an unsupported file. Nothing was installed.");
    if (![fm createDirectoryAtURL:destination withIntermediateDirectories:YES attributes:nil error:error]) return NO;
    NSArray *entries=[fm contentsOfDirectoryAtURL:source includingPropertiesForKeys:nil options:0 error:error];
    if (!entries) return NO;
    for (NSURL *entry in entries)
        if (!copyTree(entry,at(destination,entry.lastPathComponent),boundary,depth+1,error)) return NO;
    return YES;
}

@interface HelperDownload : NSObject <NSURLSessionDownloadDelegate>
@property NSURL *destination;
@property NSError *error;
@property unsigned long long expected;
@property (copy) void (^progress)(NSString *);
@property NSString *label;
@property dispatch_semaphore_t done;
@property NSTimeInterval lastUpdate;
@end
@implementation HelperDownload
- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)task didWriteData:(int64_t)bytes totalBytesWritten:(int64_t)total totalBytesExpectedToWrite:(int64_t)expected {
    if (total<0 || (unsigned long long)total>self.expected) { [task cancel]; return; }
    NSTimeInterval now=NSDate.timeIntervalSinceReferenceDate;
    if (now-self.lastUpdate<0.5) return;
    self.lastUpdate=now;
    if (self.progress) self.progress([NSString stringWithFormat:@"Downloading %@… %llu%%\nPlease keep this window open.",self.label,(unsigned long long)total*100/self.expected]);
}
- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)task didFinishDownloadingToURL:(NSURL *)location {
    NSHTTPURLResponse *response=(NSHTTPURLResponse *)task.response;
    NSNumber *size=nil; [location getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
    if (![response isKindOfClass:NSHTTPURLResponse.class] || response.statusCode!=200 || size.unsignedLongLongValue!=self.expected) {
        NSError *error=nil; fail(&error,@"A helper download was incomplete. Check your internet connection and click Install again."); self.error=error; return;
    }
    NSError *error=nil;
    [NSFileManager.defaultManager moveItemAtURL:location toURL:self.destination error:&error]; self.error=error;
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    if (error) self.error=error;
    dispatch_semaphore_signal(self.done);
}
@end

@implementation BO3SetupDependencies
+ (NSString *)runtimeArchitecture { return hostArchitecture(); }
+ (BOOL)canRunIntelCode {
    if ([hostArchitecture() isEqual:@"x86_64"]) return YES;
    NSURL *helper=at(NSBundle.mainBundle.resourceURL,@"Rosetta Check.app/Contents/MacOS/RosettaCheck");
    NSTask *task=[NSTask new];task.executableURL=helper;
    task.standardOutput=[NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
    task.standardError=[NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
    if (![task launchAndReturnError:nil]) return NO;
    [task waitUntilExit];return task.terminationStatus==0;
}
+ (BOOL)copyTree:(NSURL *)source to:(NSURL *)destination boundary:(NSURL *)boundary error:(NSError **)error {
    return copyTree(source,destination,boundary,0,error);
}
+ (NSURL *)prepare:(NSURL *)payload progress:(void (^)(NSString *))progress error:(NSError **)error {
    NSDictionary *manifest=readJSON(at(payload,@"manifest.json"));
    NSDictionary *catalog=readJSON(at(payload,@"dependencies.json"));
    if (!manifest[@"dependencies_id"]) return payload;
    if (![catalog[@"schema"] isEqual:@1] || ![catalog[@"id"] isEqual:manifest[@"dependencies_id"]] ||
        ![catalog[@"archives"] isKindOfClass:NSArray.class] || [catalog[@"archives"] count]!=4) {
        fail(error,@"The helper download list is damaged. Download the installer again."); return nil;
    }
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *work=at([NSURL fileURLWithPath:NSTemporaryDirectory()],[@"BO3MacFix-Setup-" stringByAppendingString:NSUUID.UUID.UUIDString]);
    if (![fm createDirectoryAtURL:work withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error]) return nil;
    NSURL *result=at(work,@"payload");
    BOOL ok=[fm copyItemAtURL:payload toURL:result error:error];
    NSMutableSet *seen=[NSMutableSet set];
    for (NSDictionary *archive in catalog[@"archives"]) {
        if (!ok) break;
        NSString *architecture=archive[@"host_arch"];
        if (architecture && ![@[@"arm64",@"x86_64"] containsObject:architecture]) {
            ok=fail(error,@"The helper download list is invalid. Download the installer again.");break;
        }
        if (architecture && ![architecture isEqual:hostArchitecture()]) continue;
        NSString *kind=archive[@"kind"], *hash=archive[@"sha256"];
        NSURL *url=[NSURL URLWithString:archive[@"url"] ?: @""];
        unsigned long long bytes=[archive[@"bytes"] unsignedLongLongValue];
        if (![@[@"upstream",@"wine",@"frameworks"] containsObject:kind] || [seen containsObject:kind] || ![url.scheme isEqual:@"https"] ||
            ![url.host isEqual:@"github.com"] || ![url.path containsString:@"/releases/download/"] || hash.length!=64 || bytes==0 || bytes>400000000) {
            ok=fail(error,@"The helper download list is invalid. Download the installer again."); break;
        }
        [seen addObject:kind];
        HelperDownload *download=[HelperDownload new]; download.expected=bytes; download.label=archive[@"label"] ?: @"helpers";
        download.destination=at(work,[kind stringByAppendingString:@".archive"]); download.done=dispatch_semaphore_create(0); download.progress=progress;
        NSURLSessionConfiguration *config=NSURLSessionConfiguration.ephemeralSessionConfiguration;
        config.timeoutIntervalForRequest=60; config.timeoutIntervalForResource=600;
        NSURLSession *session=[NSURLSession sessionWithConfiguration:config delegate:download delegateQueue:nil];
        [[session downloadTaskWithURL:url] resume];
        long timedOut=dispatch_semaphore_wait(download.done,dispatch_time(DISPATCH_TIME_NOW,610*NSEC_PER_SEC));
        [session invalidateAndCancel];
        if (timedOut || download.error || ![[BO3SetupEngine sha256:download.destination] isEqual:hash]) {
            ok=fail(error,@"A helper could not be downloaded and verified. Check your internet connection and click Install again."); break;
        }
        if (progress) progress([NSString stringWithFormat:@"Preparing %@…\nYour game files have not been changed yet.",download.label]);
        NSURL *extract=at(work,kind);
        ok=[fm createDirectoryAtURL:extract withIntermediateDirectories:NO attributes:nil error:error];
        if (!ok) break;
        // All exact-hash archives are inspected by tools/verify_dependency_archives.py.
        // Extract into a fresh private directory, never into the game or home directory.
        NSTask *tar=[NSTask new]; tar.executableURL=[NSURL fileURLWithPath:@"/usr/bin/tar"];
        tar.arguments=@[@"-xf",download.destination.path,@"-C",extract.path,@"--no-same-owner"];
        tar.standardOutput=[NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
        tar.standardError=[NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
        ok=[tar launchAndReturnError:error]; if (ok) { [tar waitUntilExit]; ok=tar.terminationStatus==0; }
        if (!ok) { fail(error,@"A helper could not be unpacked. Check free space and click Install again."); break; }
        if ([kind isEqual:@"upstream"]) {
            NSURL *source=at(extract,@"BO3MacFix");
            for (NSString *name in @[@"zone",@"cod2map",@"natives"])
                if (!copyTree(at(source,name),at(result,name),extract,0,error)) { ok=NO; break; }
        } else if ([kind isEqual:@"wine"]) {
            ok=copyTree(at(extract,@"wswine.bundle"),at(result,@"runtime/wine"),extract,0,error);
        } else {
            NSURL *source=at(extract,@"Template-1.0.11.app/Contents/Frameworks");
            // Only loose Wine support libraries. No renderer packs,
            // Apple GPTK components, wrapper app or authoring SDK is installed.
            NSArray *entries=[fm contentsOfDirectoryAtURL:source includingPropertiesForKeys:nil options:0 error:error];
            if (!entries.count) ok=NO;
            for (NSURL *entry in entries) {
                if (![entry.pathExtension isEqual:@"dylib"]) continue;
                if (!copyTree(entry,at(at(result,@"runtime/Frameworks"),entry.lastPathComponent),extract,0,error)) { ok=NO;break; }
            }
        }
        [fm removeItemAtURL:extract error:nil]; [fm removeItemAtURL:download.destination error:nil];
    }
    if (ok && ![seen isEqual:[NSSet setWithArray:@[@"upstream",@"wine",@"frameworks"]]])
        ok=fail(error,@"The helper download list is incomplete. Download the installer again.");
    if (ok) {
        NSMutableArray *files=[NSMutableArray array];
        NSDirectoryEnumerator *enumerator=[fm enumeratorAtURL:result includingPropertiesForKeys:@[NSURLIsDirectoryKey] options:0 errorHandler:nil];
        for (NSURL *file in enumerator) {
            NSNumber *directory=nil; [file getResourceValue:&directory forKey:NSURLIsDirectoryKey error:nil];
            if (directory.boolValue || [file.lastPathComponent isEqual:@"manifest.json"]) continue;
            NSString *hash=[BO3SetupEngine sha256:file];
            if (!hash) { ok=fail(error,@"A prepared helper is incomplete. Please try Install again."); break; }
            [files addObject:@{@"path":[file.path substringFromIndex:result.path.length+1],@"sha256":hash}];
        }
        if (ok) {
            NSMutableDictionary *prepared=[manifest mutableCopy]; prepared[@"files"]=files;
            prepared[@"requires_bundled_wine"]=@YES; prepared[@"dependencies_prepared"]=@YES;
            prepared[@"helper_arch"]=hostArchitecture();
            NSData *data=[NSJSONSerialization dataWithJSONObject:prepared options:NSJSONWritingPrettyPrinted error:error];
            ok=data && [data writeToURL:at(result,@"manifest.json") options:NSDataWritingAtomic error:error];
        }
    }
    if (!ok) { [fm removeItemAtURL:work error:nil]; return nil; }
    return result;
}
@end
