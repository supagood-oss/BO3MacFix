#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <CommonCrypto/CommonDigest.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "t7o_platform.h"
#include "t7o_save_codec.h"
#include "t7o_json.h"
#include <stdatomic.h>
#ifndef T7O_EXPERIMENTAL_MOD_TEST
#define T7O_EXPERIMENTAL_MOD_TEST 0
#endif
static atomic_uint engine_capabilities;
void t7o_platform_engine_capabilities(uint32_t capabilities) { atomic_store(&engine_capabilities,capabilities); }

static NSObject *http_lock;
static NSMutableDictionary<NSString *, NSMutableDictionary *> *jobs;
static NSUInteger generation;
static BOOL legacy_codec;
static T7oSaveKey save_keys[2];
static NSUInteger legacy_ticket;
static char *(*ticket_provider)(void);
void t7o_platform_ticket_provider(char *(*provider)(void)) { ticket_provider=provider; }
static BOOL stats_origin(NSURL *url) {
    return [url.scheme.lowercaseString isEqual:@"https"] && [url.host.lowercaseString isEqual:@"deadhighstats.com"];
}
@interface T7oRedirectGuard : NSObject <NSURLSessionTaskDelegate>
@end
@implementation T7oRedirectGuard
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request
    completionHandler:(void (^)(NSURLRequest *))completion {
    (void)session; (void)response;
    NSMutableURLRequest *next=[request mutableCopy];
    if (!stats_origin(next.URL)) {
        [next setValue:nil forHTTPHeaderField:@"X-Steam-Ticket"];
        [next setValue:nil forHTTPHeaderField:@"X-Steam-AppID"];
    }
    /* Never send a ticket-bearing request to another origin, including an
     * HTTPS-to-HTTP redirect. The map can retry without exposing credentials. */
    if ([task.originalRequest valueForHTTPHeaderField:@"X-Steam-Ticket"].length && !stats_origin(next.URL)) completion(nil);
    else completion(next);
}
@end
#ifdef T7O_TESTING
static NSURLSession *test_session;
void t7o_platform_test_session(NSURLSession *session) { test_session=session; }
#endif
static NSURLSession *session(void) {
#ifdef T7O_TESTING
    if (test_session) return test_session;
#endif
    static NSURLSession *transport;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        transport=[NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.ephemeralSessionConfiguration
            delegate:[T7oRedirectGuard new] delegateQueue:nil];
    });
    return transport;
}

static void initialize(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        http_lock=[NSObject new]; jobs=[NSMutableDictionary new];
    });
}
static char *copy_string(NSString *value) { return strdup((value ?: @"").UTF8String ?: ""); }
static NSString *arg(size_t argc, const char *const *argv, size_t index) {
    return index < argc && argv[index] ? [[NSString alloc] initWithUTF8String:argv[index]] : nil;
}
static NSTimeInterval timeout_value(NSString *text) {
    double value = text.doubleValue;
    return isfinite(value) && value > 0 ? MIN(value, 120) : 5;
}
static NSMutableURLRequest *request(NSString *method, NSString *url, NSString *body, NSString *timeout) {
    NSURL *address = [NSURL URLWithString:url ?: @""];
    NSString *scheme = address.scheme.lowercaseString;
    if (!address.host.length || (![scheme isEqual:@"https"] && ![scheme isEqual:@"http"])) return nil;
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:address
        cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:timeout_value(timeout)];
    req.HTTPMethod = method;
    if ([method isEqual:@"POST"]) {
        req.HTTPBody = [(body ?: @"") dataUsingEncoding:NSUTF8StringEncoding];
        // The current AAE native POST helper explicitly uses application/json
        // (RVA 0x14416a in the hash-pinned native image).
        [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    }
    return req;
}
static NSString *response_text(NSData *data, NSURLResponse *response, NSError *error) {
    if (error || ![response isKindOfClass:NSHTTPURLResponse.class]) return @"";
    NSInteger status = ((NSHTTPURLResponse *)response).statusCode;
    if (status < 200 || status >= 300) return @"";
    return [[NSString alloc] initWithData:data ?: [NSData data] encoding:NSUTF8StringEncoding] ?: @"";
}
void t7o_platform_begin_mod(const char *dll_path) {
    atomic_store(&engine_capabilities,0);
    initialize();
    @synchronized(http_lock) {
        ++generation;
        for (NSDictionary *entry in jobs.allValues) [entry[@"task"] cancel];
        [jobs removeAllObjects];
        legacy_codec=NO; memset(save_keys,0,sizeof(save_keys));
        if (dll_path) {
            NSString *path=[[NSString alloc] initWithUTF8String:dll_path];
            NSDictionary *attributes=path ? [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil] : nil;
            NSData *data=path && [attributes[NSFileType] isEqual:NSFileTypeRegular] &&
                [attributes[NSFileSize] unsignedLongLongValue]<=16*1024*1024 ? [NSData dataWithContentsOfFile:path] : nil;
            if (data) {
                unsigned char digest[CC_SHA256_DIGEST_LENGTH];
                CC_SHA256(data.bytes,(CC_LONG)data.length,digest);
                NSMutableString *hash=[NSMutableString string];
                for (size_t i=0;i<sizeof(digest);++i) [hash appendFormat:@"%02x",digest[i]];
                if ([hash isEqual:@"9a115ad7b7cb20c01b5bcaec66777467f570171df0a5bdf2972f334c9f66214f"])
                    legacy_codec=t7o_save_keys(data.bytes,data.length,save_keys);
            }
        }
    }
}
#ifdef T7O_TESTING
void t7o_platform_test_save_codec(void) {
    initialize(); @synchronized(http_lock) {
        save_keys[0]=(T7oSaveKey){0x1c1b35e,0x17ff};
        save_keys[1]=(T7oSaveKey){123456,4567};
        legacy_codec=YES;
    }
}
#endif
static NSString *job_name(NSString *name) {
    NSArray *parts=[name componentsSeparatedByString:@";"];
    return parts.count==3 ? parts[0] : name;
}
static NSMutableURLRequest *legacy_request(NSString *method,NSString *url,NSString *body,NSString *version,NSString *timeout) {
    NSMutableURLRequest *req=request(method,url,body,timeout);
    if (!req) return nil;
    [req setValue:[@"DeadHighGameClient/" stringByAppendingString:version ?: @""] forHTTPHeaderField:@"User-Agent"];
    [req setValue:@"Mac" forHTTPHeaderField:@"X-Exe-Version"];
    if (legacy_codec && stats_origin(req.URL) && ticket_provider) {
        char *ticket=ticket_provider();
        if (ticket) {
            [req setValue:[NSString stringWithUTF8String:ticket] forHTTPHeaderField:@"X-Steam-Ticket"];
            [req setValue:@"311210" forHTTPHeaderField:@"X-Steam-AppID"];
            memset(ticket,0,strlen(ticket)); free(ticket);
        }
    }
    return req;
}
static NSString *legacy_body(NSData *data,NSURLResponse *response,NSError *error) {
    if (error || ![response isKindOfClass:NSHTTPURLResponse.class]) return @"";
    return [[NSString alloc] initWithData:data ?: [NSData data] encoding:NSUTF8StringEncoding] ?: @"";
}
static NSString *legacy_result(NSData *data,NSURLResponse *response,NSError *error) {
    NSInteger status=[response isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *)response).statusCode : 0;
    return [NSString stringWithFormat:@"%@\n%ld\n%@",!error && status>=200 && status<300 ? @"OK" : @"ERROR",
        (long)status,legacy_body(data,response,error)];
}
static char *sync_request(NSMutableURLRequest *req,BOOL legacy) {
    if (!req) return strdup("");
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block NSString *text = @"";
    NSURLSessionDataTask *task = [session() dataTaskWithRequest:req
        completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            text = legacy ? legacy_body(data,response,error) : response_text(data, response, error);
            dispatch_semaphore_signal(done);
        }];
    [task resume];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW,
        (int64_t)((req.timeoutInterval + 1) * NSEC_PER_SEC))) != 0) {
        [task cancel]; return strdup("");
    }
    return copy_string(text);
}
static char *get_sync(NSString *url,NSString *timeout) { return sync_request(request(@"GET",url,nil,timeout),NO); }
static char *submit_request(NSMutableURLRequest *req,NSString *name,BOOL legacy) {
    if (!req || !name.length) return strdup("0");
    initialize();
    @synchronized(http_lock) {
        if (jobs.count >= 256 && !jobs[name]) return strdup("0");
        [jobs[name][@"task"] cancel];
        NSUInteger request_generation = generation;
        NSString *token = NSUUID.UUID.UUIDString;
        NSMutableDictionary *entry = [@{@"token":token, @"text":legacy ? @"PENDING" : @"",@"legacy":@(legacy)} mutableCopy];
        NSURLSessionDataTask *task = [session() dataTaskWithRequest:req
            completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                @synchronized(http_lock) {
                    NSMutableDictionary *current = jobs[name];
                    if (request_generation == generation && [current[@"token"] isEqual:token]) {
                        current[@"text"] = legacy ? legacy_result(data,response,error) : response_text(data, response, error);
                        [current removeObjectForKey:@"task"];
                        if ([current[@"discard"] boolValue]) [jobs removeObjectForKey:name];
                    }
                }
            }];
        entry[@"task"] = task;
        jobs[name] = entry;
        [task resume];
    }
    return strdup("1");
}
static char *submit(NSString *method,NSString *name,NSString *url,NSString *body,NSString *timeout) {
    if (![method isEqual:@"GET"] && ![method isEqual:@"POST"]) return strdup("0");
    return submit_request(request(method,url,body,timeout),name,NO);
}
static NSString *hardware_id(void) {
    io_service_t platform = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    if (!platform) return nil;
    CFTypeRef value = IORegistryEntryCreateCFProperty(platform, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
    IOObjectRelease(platform);
    if (!value) return nil;
    NSString *uuid = CFGetTypeID(value) == CFStringGetTypeID() ? [(__bridge NSString *)value copy] : nil;
    CFRelease(value);
    if (!uuid.length) return nil;
    NSData *data = [uuid dataUsingEncoding:NSUTF8StringEncoding];
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString *identity = [NSMutableString stringWithString:@"mac-"];
    for (size_t i=0; i<sizeof(digest); ++i) [identity appendFormat:@"%02x", digest[i]];
    return identity;
}
char *t7o_platform_call(const char *operation, size_t argc, const char *const *argv) {
    @autoreleasepool {
        if (!strcmp(operation,"json.read") || !strcmp(operation,"json.write")) return t7o_json_call(operation,argc,argv);
        if (strcmp(operation,"capability")==0) {
            NSString *name=arg(argc,argv,0);
            NSArray *names=@[@"client_fx",@"csc_bridge",@"movement",@"pause",@"mod_unload",@"aim_status"];
            NSUInteger index=[names indexOfObject:name ?: @""];
            if (index!=NSNotFound) return strdup((atomic_load(&engine_capabilities)&(1u<<index)) ? "1" : "0");
            if ([name isEqual:@"save_codec"]) return strdup(legacy_codec ? "1" : "0");
            return strdup([name isEqual:@"http"] || [name isEqual:@"fileio"] ? "1" : "0");
        }
        /* Only the explicitly built experimental candidate may pass the mod's
         * API revision check, and only after all required adapters activate.
         * Live co-op qualification is recorded separately from API revision. */
        if (strcmp(operation,"revision")==0)
            return strdup(T7O_EXPERIMENTAL_MOD_TEST && atomic_load(&engine_capabilities)==T7O_CAP_ALL ? "651" : "0");
        if (strcmp(operation,"status")==0)
            return strdup(T7O_EXPERIMENTAL_MOD_TEST && atomic_load(&engine_capabilities)==T7O_CAP_ALL ?
                "experimental Mac test candidate; live co-op unverified" : "required Mac native adapters are unavailable or testing is disabled");
        if (strcmp(operation,"system.hardware_id")==0) {
            NSString *identity=hardware_id(); return identity ? copy_string(identity) : NULL;
        }
        if (strcmp(operation,"system.cpu")==0) {
            char brand[256]={0}; size_t size=sizeof(brand)-1;
            if (sysctlbyname("machdep.cpu.brand_string",brand,&size,NULL,0)!=0) return strdup("unknown Mac CPU");
            brand[sizeof(brand)-1]=0; return strdup(brand);
        }
        if (strcmp(operation,"module.loaded")==0) {
            NSString *name=arg(argc,argv,0); if (!name.length) return strdup("0");
            for (uint32_t i=0; i<_dyld_image_count(); ++i) {
                NSString *image=[NSString stringWithUTF8String:_dyld_get_image_name(i)];
                if ([image.lastPathComponent caseInsensitiveCompare:name]==NSOrderedSame) return strdup("1");
            }
            return strdup("0");
        }
        if (strcmp(operation,"http.get")==0) return get_sync(arg(argc,argv,0),arg(argc,argv,1));
        if (strcmp(operation,"save.encode")==0 || strcmp(operation,"save.decode")==0) {
            initialize(); @synchronized(http_lock) {
                if (!legacy_codec || !argc || !argv[0]) return NULL;
                return !strcmp(operation,"save.encode") ? t7o_save_encode(argv[0],save_keys[0]) : t7o_save_decode(argv[0],save_keys);
            }
        }
        if (strcmp(operation,"legacy.get")==0)
            return sync_request(legacy_request(@"GET",arg(argc,argv,0),nil,arg(argc,argv,1),@"5"),YES);
        if (strcmp(operation,"legacy.post_response")==0)
            return sync_request(legacy_request(@"POST",arg(argc,argv,0),arg(argc,argv,1),arg(argc,argv,2),@"5"),YES);
        if (strcmp(operation,"legacy.post_queued")==0 || strcmp(operation,"legacy.post")==0) {
            initialize(); @synchronized(http_lock) {
                if (jobs.count>=256) return strdup("0");
                if (++legacy_ticket>15999999) legacy_ticket=1;
                NSString *name=[NSString stringWithFormat:@"legacy-%lu",(unsigned long)legacy_ticket];
                double ms=arg(argc,argv,3).doubleValue;
                NSString *timeout=[NSString stringWithFormat:@"%.3f",isfinite(ms) && ms>0 ? MIN(ms/1000,120) : 10];
                char *ok=submit_request(legacy_request(@"POST",arg(argc,argv,0),arg(argc,argv,1),arg(argc,argv,2),timeout),name,YES);
                BOOL started=ok && !strcmp(ok,"1"); free(ok);
                if (!strcmp(operation,"legacy.post")) {
                    if (started) jobs[name][@"discard"]=@YES;
                    return strdup(started ? "POST request started" : "");
                }
                return started ? copy_string([NSString stringWithFormat:@"%lu",(unsigned long)legacy_ticket]) : strdup("0");
            }
        }
        if (strcmp(operation,"legacy.result")==0) {
            initialize(); NSString *name=[@"legacy-" stringByAppendingString:arg(argc,argv,0) ?: @"0"];
            @synchronized(http_lock) {
                NSDictionary *entry=jobs[name];
                NSString *text=entry[@"text"] ?: @"UNKNOWN\n0\n";
                if (entry && ![text isEqual:@"PENDING"]) [jobs removeObjectForKey:name];
                return copy_string(text);
            }
        }
        if (strcmp(operation,"http.submit")==0)
            return submit(arg(argc,argv,0),job_name(arg(argc,argv,1)),arg(argc,argv,2),arg(argc,argv,3),arg(argc,argv,4));
        if (strcmp(operation,"http.retrieve")==0) {
            initialize(); NSString *name=job_name(arg(argc,argv,0));
            @synchronized(http_lock) { return copy_string(name ? jobs[name][@"text"] : nil); }
        }
        return NULL;
    }
}
