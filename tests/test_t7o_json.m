#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdlib.h>
#include "../source/t7o_json.h"
static NSString *call(const char *op,NSArray<NSString *> *args) {
    const char *argv[6]; assert(args.count<=6);
    for (NSUInteger i=0;i<args.count;++i) argv[i]=args[i].UTF8String;
    char *value=t7o_json_call(op,args.count,argv);
    NSString *result=value ? [NSString stringWithUTF8String:value] : nil;
    free(value); return result;
}
int main(void) {
    @autoreleasepool {
        NSString *root=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        NSString *path=[root stringByAppendingPathComponent:@"players/t7.json"];
        assert([call("json.read",@[path,@"",@"missing",@"bool",@"",@"1"]) isEqual:@"0"]);
        assert(![NSFileManager.defaultManager fileExistsAtPath:root]);
        assert([call("json.write",@[path,@"",@"discord_enabled",@"bool",@"false"]) isEqual:@"1"]);
        assert([call("json.read",@[path,@"",@"discord_enabled",@"boolean"]) isEqual:@"0"]);
        assert([call("json.write",@[path,@"developer",@"key",@"string",@"café ✓"]) isEqual:@"1"]);
        assert([call("json.read",@[path,@"developer",@"key",@"string"]) isEqual:@"café ✓"]);
        assert([call("json.write",@[path,@"",@"counter",@"uint64",@"18446744073709551615"]) isEqual:@"1"]);
        assert([call("json.read",@[path,@"",@"counter",@"uint64_t"]) isEqual:@"18446744073709551615"]);
        assert([call("json.read",@[path,@"developer",@"key",@"string"]) isEqual:@"café ✓"]);
        NSData *before=[NSData dataWithContentsOfFile:path];
        assert([call("json.read",@[path,@"missing_section",@"missing",@"int",@"12",@"1"]) isEqual:@"12"]);
        assert([[NSData dataWithContentsOfFile:path] isEqual:before]);
        assert([call("json.read",@[path,@"new_section",@"created",@"float",@"1.25",@"0"]) isEqual:@"1.25"]);
        assert([call("json.read",@[path,@"new_section",@"created",@"float"]) isEqual:@"1.25"]);
        before=[NSData dataWithContentsOfFile:path];
        assert(call("json.write",@[path,@"",@"invalid",@"uint",@"-1"])==nil);
        assert(call("json.write",@[path,@"",@"invalid",@"int",@"2147483648"])==nil);
        assert(call("json.write",@[path,@"",@"invalid",@"double",@"nan"])==nil);
        assert(call("json.write",@[path,@"",@"invalid",@"unknown",@"12"])==nil);
        assert([[NSData dataWithContentsOfFile:path] isEqual:before]);
        assert([@"malformed saved file" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil]);
        before=[NSData dataWithContentsOfFile:path];
        assert(call("json.write",@[path,@"",@"keep",@"bool",@"1"])==nil);
        assert(call("json.read",@[path,@"",@"keep",@"bool"])==nil);
        assert([[NSData dataWithContentsOfFile:path] isEqual:before]);
        assert([NSFileManager.defaultManager removeItemAtPath:root error:nil]);
        puts("JSON config strings/types, read-only defaults, atomic updates and malformed-file preservation passed");
    }
    return 0;
}
