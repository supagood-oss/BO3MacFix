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
static BOOL helperFailure(NSError **error, NSString *label, NSString *stage, NSError *cause) {
    NSMutableDictionary *info=[@{NSLocalizedDescriptionKey:[NSString stringWithFormat:@"Could not %@ %@. %@",stage,label,
        cause.localizedDescription ?: @"Please try Install again."],
        @"BO3SetupHelper":label,@"BO3SetupStage":stage} mutableCopy];
    if (cause) info[NSUnderlyingErrorKey]=cause;
    if (error) *error=[NSError errorWithDomain:@"BO3MacFix Setup" code:2 userInfo:info];
    return NO;
}
static BOOL retryableDownload(NSError *error) {
    if ([error.domain isEqual:NSURLErrorDomain])
        return [@[@(NSURLErrorTimedOut),@(NSURLErrorNetworkConnectionLost),@(NSURLErrorCannotConnectToHost),
            @(NSURLErrorCannotFindHost),@(NSURLErrorDNSLookupFailed),@(NSURLErrorNotConnectedToInternet)] containsObject:@(error.code)];
    return [error.domain isEqual:@"BO3MacFix HTTP"] && [@[@408,@429,@500,@502,@503,@504] containsObject:@(error.code)];
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
    if (![response isKindOfClass:NSHTTPURLResponse.class] || response.statusCode!=200) {
        NSInteger status=[response isKindOfClass:NSHTTPURLResponse.class] ? response.statusCode : 0;
        self.error=[NSError errorWithDomain:@"BO3MacFix HTTP" code:status userInfo:@{NSLocalizedDescriptionKey:
            [NSString stringWithFormat:@"The download server returned HTTP %ld. Please try again later.",(long)status]}];
        return;
    }
    if (size.unsignedLongLongValue!=self.expected) {
        NSError *error=nil; fail(&error,@"The download was incomplete. Check your internet connection and click Install again."); self.error=error; return;
    }
    NSError *error=nil;
    [NSFileManager.defaultManager moveItemAtURL:location toURL:self.destination error:&error]; self.error=error;
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    if (error) self.error=error;
    dispatch_semaphore_signal(self.done);
}
@end

#ifdef BO3SETUP_TESTING
@interface BO3SetupFixtureDownloadTask : NSObject
@property NSURLResponse *response;
@end
@implementation BO3SetupFixtureDownloadTask
@end
BOOL BO3SetupTestRetryableDownload(NSError *error) { return retryableDownload(error); }
NSError *BO3SetupTestDownloadResponse(NSURL *file, NSURL *destination, NSInteger status, unsigned long long bytes) {
    HelperDownload *download=[HelperDownload new];download.destination=destination;download.expected=bytes;
    BO3SetupFixtureDownloadTask *task=[BO3SetupFixtureDownloadTask new];
    task.response=[[NSHTTPURLResponse alloc] initWithURL:[NSURL URLWithString:@"https://example.invalid/helper"]
        statusCode:status HTTPVersion:@"HTTP/1.1" headerFields:nil];
    [download URLSession:nil downloadTask:(id)task didFinishDownloadingToURL:file];
    return download.error;
}
#endif

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
+ (NSArray<NSDictionary *> *)fileRecords:(NSURL *)payload error:(NSError **)error {
    // NSURL enumeration can expand /var to /private/var on macOS. Subtracting
    // the unresolved root length produced bogus "payload/..." manifest paths.
    NSURL *root=payload.URLByResolvingSymlinksInPath;
    NSString *prefix=[root.path stringByAppendingString:@"/"];
    NSMutableArray *files=[NSMutableArray array];
    __block NSError *walkError=nil;
    NSDirectoryEnumerator *enumerator=[NSFileManager.defaultManager enumeratorAtURL:root
        includingPropertiesForKeys:@[NSURLIsDirectoryKey] options:0 errorHandler:^BOOL(NSURL *url,NSError *cause) {
            walkError=cause;return NO;
        }];
    if (!enumerator) { fail(error,@"The prepared helper folder could not be read. Please try Install again.");return nil; }
    for (NSURL *file in enumerator) {
        NSNumber *directory=nil;
        if (![file getResourceValue:&directory forKey:NSURLIsDirectoryKey error:error]) return nil;
        if (directory.boolValue || [file.lastPathComponent isEqual:@"manifest.json"]) continue;
        if (![file.path hasPrefix:prefix]) { fail(error,@"A prepared helper path could not be resolved. Nothing was installed.");return nil; }
        NSString *hash=[BO3SetupEngine sha256:file];
        if (!hash) { fail(error,@"A prepared helper is incomplete. Please try Install again.");return nil; }
        [files addObject:@{@"path":[file.path substringFromIndex:prefix.length],@"sha256":hash}];
    }
    if (walkError) { if (error) *error=walkError;return nil; }
    return files;
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
        NSString *label=archive[@"label"] ?: @"helpers";
        NSURL *archiveFile=at(work,[kind stringByAppendingString:@".archive"]);
        NSError *downloadError=nil;
        for (unsigned attempt=0;attempt<2;++attempt) {
            if (progress) progress([NSString stringWithFormat:attempt ? @"Retrying %@…" : @"Downloading %@…",label]);
            HelperDownload *download=[HelperDownload new]; download.expected=bytes; download.label=label;
            download.destination=archiveFile; download.done=dispatch_semaphore_create(0); download.progress=progress;
            NSURLSessionConfiguration *config=NSURLSessionConfiguration.ephemeralSessionConfiguration;
            // A 166 MB helper takes over ten minutes on a 2 Mbps connection.
            // Allow slow but advancing downloads; still bound idle/network time.
            config.timeoutIntervalForRequest=120; config.timeoutIntervalForResource=3600;
            NSURLSession *session=[NSURLSession sessionWithConfiguration:config delegate:download delegateQueue:nil];
            [[session downloadTaskWithURL:url] resume];
            dispatch_semaphore_wait(download.done,DISPATCH_TIME_FOREVER);
            [session finishTasksAndInvalidate];
            downloadError=download.error;
            if (!downloadError || !retryableDownload(downloadError)) break;
        }
        if (downloadError) {
            ok=helperFailure(error,label,@"download",downloadError); break;
        }
        if (![[BO3SetupEngine sha256:archiveFile] isEqual:hash]) {
            ok=helperFailure(error,label,@"verify",[NSError errorWithDomain:@"BO3MacFix Download" code:1
                userInfo:@{NSLocalizedDescriptionKey:@"The downloaded file did not match the expected checksum. Nothing was installed; please try again."}]); break;
        }
        if (progress) progress([NSString stringWithFormat:@"Preparing %@…\nYour game files have not been changed yet.",label]);
        NSURL *extract=at(work,kind);
        ok=[fm createDirectoryAtURL:extract withIntermediateDirectories:NO attributes:nil error:error];
        if (!ok) break;
        // All exact-hash archives are inspected by tools/verify_dependency_archives.py.
        // Extract into a fresh private directory, never into the game or home directory.
        NSTask *tar=[NSTask new]; tar.executableURL=[NSURL fileURLWithPath:@"/usr/bin/tar"];
        tar.arguments=@[@"-xf",archiveFile.path,@"-C",extract.path,@"--no-same-owner"];
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
        [fm removeItemAtURL:extract error:nil]; [fm removeItemAtURL:archiveFile error:nil];
    }
    if (ok && ![seen isEqual:[NSSet setWithArray:@[@"upstream",@"wine",@"frameworks"]]])
        ok=fail(error,@"The helper download list is incomplete. Download the installer again.");
    if (ok) {
        NSArray *files=[self fileRecords:result error:error];
        ok=files!=nil;
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
