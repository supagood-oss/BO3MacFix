#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "../source/t7o_platform.h"
void t7o_platform_test_session(NSURLSession *session);
void t7o_platform_test_save_codec(void);

static atomic_uint requests_started;

@interface LocalResponse : NSURLProtocol
@property BOOL stopped;
@end
@implementation LocalResponse
+ (BOOL)canInitWithRequest:(NSURLRequest *)request { return YES; }
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request { return request; }
- (void)startLoading {
    atomic_fetch_add(&requests_started,1);
    NSTimeInterval delay=[self.request.URL.path isEqual:@"/slow"] ? 0.2 : 0.01;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(delay*NSEC_PER_SEC)),
        dispatch_get_global_queue(QOS_CLASS_DEFAULT,0), ^{
            if (self.stopped) return;
            NSInteger status=[self.request.URL.path isEqual:@"/error"] ? 500 : 200;
            NSHTTPURLResponse *response=[[NSHTTPURLResponse alloc] initWithURL:self.request.URL statusCode:status
                HTTPVersion:@"HTTP/1.1" headerFields:@{}];
            [self.client URLProtocol:self didReceiveResponse:response cacheStoragePolicy:NSURLCacheStorageNotAllowed];
            NSString *text=[self.request.URL.path stringByAppendingString:@" ✓"];
            if ([self.request.URL.path isEqual:@"/headers"]) {
                text=[NSString stringWithFormat:@"%@|%@|%@|%@",[self.request valueForHTTPHeaderField:@"User-Agent"],
                    [self.request valueForHTTPHeaderField:@"X-Exe-Version"],
                    [self.request valueForHTTPHeaderField:@"X-Steam-Ticket"] ?: @"",
                    [self.request valueForHTTPHeaderField:@"X-Steam-AppID"] ?: @""];
            }
            if ([self.request.HTTPMethod isEqual:@"POST"]) {
                NSData *bodyBytes=self.request.HTTPBody;
                if (!bodyBytes && self.request.HTTPBodyStream) {
                    NSInputStream *input=self.request.HTTPBodyStream;NSMutableData *bytes=[NSMutableData data];
                    [input open];uint8_t buffer[1024];NSInteger count;
                    while ((count=[input read:buffer maxLength:sizeof(buffer)])>0 && bytes.length<65536)
                        [bytes appendBytes:buffer length:(NSUInteger)count];
                    [input close];bodyBytes=bytes;
                }
                text=[NSString stringWithFormat:@"%@|%@|%@",self.request.URL.path,
                    [self.request valueForHTTPHeaderField:@"Content-Type"],
                    [[NSString alloc] initWithData:bodyBytes ?: [NSData data] encoding:NSUTF8StringEncoding]];
            }
            NSData *body=[text dataUsingEncoding:NSUTF8StringEncoding];
            [self.client URLProtocol:self didLoadData:body];
            [self.client URLProtocolDidFinishLoading:self];
        });
}
- (void)stopLoading { self.stopped=YES; }
@end
static NSString *call(const char *operation, NSArray<NSString *> *arguments) {
    const char *argv[7]; assert(arguments.count<=7);
    for (NSUInteger i=0;i<arguments.count;++i) argv[i]=arguments[i].UTF8String;
    char *result=t7o_platform_call(operation,arguments.count,argv);
    NSString *text=result ? [NSString stringWithUTF8String:result] : nil;
    free(result); return text;
}
static NSString *poll(NSString *name) {
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:2]; NSString *result;
    do {
        result=call("http.retrieve",@[name]); if (result.length) return result;
        [NSThread sleepForTimeInterval:0.005];
    } while (deadline.timeIntervalSinceNow>0);
    return result;
}
static char *fixture_ticket(void) { return strdup("fixture_only_not_a_real_ticket"); }
static NSString *poll_legacy(NSString *ticket) {
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:2]; NSString *result;
    do {
        result=call("legacy.result",@[ticket]); if (![result isEqual:@"PENDING"]) return result;
        [NSThread sleepForTimeInterval:0.005];
    } while (deadline.timeIntervalSinceNow>0);
    return result;
}
@interface T7oRedirectGuard : NSObject <NSURLSessionTaskDelegate>
@end
int main(void) {
    @autoreleasepool {
        NSURLSessionConfiguration *config=NSURLSessionConfiguration.ephemeralSessionConfiguration;
        config.protocolClasses=@[LocalResponse.class];
        NSURLSession *transport=[NSURLSession sessionWithConfiguration:config];
        t7o_platform_test_session(transport);
        t7o_platform_begin_mod(NULL);
        assert([call("revision",@[]) isEqual:@"0"]);
        t7o_platform_engine_capabilities(T7O_CAP_ALL);
        assert([call("revision",@[]) isEqual:@"651"]);
        assert([call("capability",@[@"movement"]) isEqual:@"1"]);
        t7o_platform_engine_capabilities(T7O_CAP_ALL & ~T7O_CAP_UNLOAD);
        assert([call("revision",@[]) isEqual:@"0"]);
        t7o_platform_begin_mod(NULL);
        assert([call("capability",@[@"movement"]) isEqual:@"0"]);
        assert([call("http.retrieve",@[@"missing"]) isEqual:@""]);
        assert([call("http.get",@[@"https://example.invalid/sync",@"1"]) isEqual:@"/sync ✓"]);
        assert([call("http.submit",@[@"GET",@"job",@"https://example.invalid/slow",@"",@"1"]) isEqual:@"1"]);
        assert([call("http.submit",@[@"GET",@"job",@"https://example.invalid/new",@"",@"1"]) isEqual:@"1"]);
        assert([poll(@"job") isEqual:@"/new ✓"]);
        [NSThread sleepForTimeInterval:0.25];
        assert([call("http.retrieve",@[@"job"]) isEqual:@"/new ✓"]);
        call("http.submit",@[@"POST",@"old",@"https://example.invalid/slow",@"a=1",@"1"]);
        t7o_platform_begin_mod(NULL);
        [NSThread sleepForTimeInterval:0.25];
        assert([call("http.retrieve",@[@"old"]) isEqual:@""]);
        assert([call("http.submit",@[@"GET",@"bad",@"steam://joinlobby/311210/1",@"",@"1"]) isEqual:@"0"]);
        unsigned before=atomic_load(&requests_started);
        assert([call("capability",@[@"encrypted_http"]) isEqual:@"0"]);
        assert([call("capability",@[@"http_asset"]) isEqual:@"0"]);
        for (NSString *selector in @[@"",@"fixture_selector",@"arbitrary_new_name"]) {
            assert(call("http.get_encrypted",@[@"https://example.invalid/value",selector,@"1"])==nil);
            assert(call("http.submit_encrypted",@[@"POST",@"blocked",@"https://example.invalid/value",
                @"fixture_body",selector,selector,@"1"])==nil);
        }
        assert(atomic_load(&requests_started)==before);
        assert([call("http.submit",@[@"GET",@"plain;plain;plain",@"https://example.invalid/named",@"",@"1"]) isEqual:@"1"]);
        assert([poll(@"plain") isEqual:@"/named ✓"]);
        assert(atomic_load(&requests_started)>before);
        t7o_platform_begin_mod(NULL);
        t7o_platform_test_save_codec();
        t7o_platform_ticket_provider(fixture_ticket);
        assert([call("capability",@[@"save_codec"]) isEqual:@"1"]);
        NSString *encoded=call("save.encode",@[@"{\"bank\":12}"]);
        assert([call("save.decode",@[encoded]) isEqual:@"{\"bank\":12}"]);
        assert(call("save.decode",@[@"not valid hex"])==nil);
        assert([call("legacy.get",@[@"https://deadhighstats.com/headers",@"1.25"])
            isEqual:@"DeadHighGameClient/1.25|Mac|fixture_only_not_a_real_ticket|311210"]);
        assert([call("legacy.get",@[@"https://example.invalid/headers",@"1.25"])
            isEqual:@"DeadHighGameClient/1.25|Mac||"]);
        assert([call("legacy.get",@[@"http://deadhighstats.com/headers",@"1.25"])
            isEqual:@"DeadHighGameClient/1.25|Mac||"]);
        assert([call("legacy.post_response",@[@"https://example.invalid/post",@"hex text",@"1.25"])
            isEqual:@"/post|application/json|hex text"]);
        NSString *ticket=call("legacy.post_queued",@[@"https://example.invalid/slow",@"body",@"1.25",@"1000"]);
        assert(ticket.intValue>0);
        assert([call("legacy.result",@[ticket]) isEqual:@"PENDING"]);
        assert([poll_legacy(ticket) isEqual:@"OK\n200\n/slow|application/json|body"]);
        assert([call("legacy.result",@[ticket]) isEqual:@"UNKNOWN\n0\n"]);
        ticket=call("legacy.post_queued",@[@"https://example.invalid/error",@"error body",@"1.25",@"1000"]);
        assert([poll_legacy(ticket) isEqual:@"ERROR\n500\n/error|application/json|error body"]);
        ticket=call("legacy.post_queued",@[@"https://example.invalid/slow",@"body",@"1.25",@"1000"]);
        t7o_platform_begin_mod(NULL);
        [NSThread sleepForTimeInterval:0.25];
        assert([call("legacy.result",@[ticket]) isEqual:@"UNKNOWN\n0\n"]);
        assert([call("capability",@[@"save_codec"]) isEqual:@"0"]);
        assert(call("save.encode",@[@"{}"])==nil);
        NSMutableURLRequest *original=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"https://deadhighstats.com/headers"]];
        [original setValue:@"fixture only" forHTTPHeaderField:@"X-Steam-Ticket"];
        NSURLSessionDataTask *redirectTask=[transport dataTaskWithRequest:original];
        T7oRedirectGuard *guard=[T7oRedirectGuard new];
        NSHTTPURLResponse *redirectResponse=[[NSHTTPURLResponse alloc] initWithURL:original.URL statusCode:302 HTTPVersion:@"HTTP/1.1" headerFields:@{}];
        [guard URLSession:transport task:redirectTask willPerformHTTPRedirection:redirectResponse
            newRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"https://example.invalid/headers"]]
            completionHandler:^(NSURLRequest *request) { assert(request==nil); }];
        [redirectTask cancel];
        [transport invalidateAndCancel];
        puts("macOS HTTP transport contract tests passed; no backend contacted");
    }
    return 0;
}
