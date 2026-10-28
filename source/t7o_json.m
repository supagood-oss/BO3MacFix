#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include "t7o_json.h"

static NSString *argument(size_t argc,const char *const *argv,size_t index) {
    return index<argc && argv[index] ? [NSString stringWithUTF8String:argv[index]] : @"";
}
typedef enum { K_INVALID,K_STRING,K_BOOL,K_INT,K_UINT,K_INT64,K_UINT64,K_FLOAT,K_DOUBLE } Kind;
static Kind kind(NSString *name) {
    if ([name isEqual:@"string"]) return K_STRING;
    if ([name isEqual:@"bool"] || [name isEqual:@"boolean"]) return K_BOOL;
    if ([@[@"int",@"int32",@"int32_t"] containsObject:name]) return K_INT;
    if ([@[@"uint",@"unsigned",@"uint32",@"uint32_t"] containsObject:name]) return K_UINT;
    if ([@[@"int64",@"int64_t"] containsObject:name]) return K_INT64;
    if ([@[@"uint64",@"uint64_t"] containsObject:name]) return K_UINT64;
    if ([name isEqual:@"float"]) return K_FLOAT;
    if ([name isEqual:@"double"]) return K_DOUBLE;
    return K_INVALID;
}
static NSNumber *number(NSString *text,Kind type) {
    const char *start=text.UTF8String ?: ""; char *end;
    errno=0;
    if (type==K_BOOL) {
        NSString *value=text.lowercaseString;
        if ([@[@"true",@"1",@"yes",@"on"] containsObject:value]) return @YES;
        return @NO;
    }
    if (!*start) return @0;
    if (type==K_FLOAT || type==K_DOUBLE) {
        double value=strtod(start,&end);
        if (errno || end==start || *end || !isfinite(value)) return nil;
        if (type==K_FLOAT) { value=(float)value; if (!isfinite(value)) return nil; }
        return @(value);
    }
    if (type==K_UINT || type==K_UINT64) {
        if (*start=='-') return nil;
        unsigned long long value=strtoull(start,&end,10);
        if (errno || end==start || *end || (type==K_UINT && value>UINT32_MAX)) return nil;
        return @(value);
    }
    long long value=strtoll(start,&end,10);
    if (errno || end==start || *end || (type==K_INT && (value<INT32_MIN || value>INT32_MAX))) return nil;
    return @(value);
}
static id scalar(NSString *text,Kind type) { return type==K_STRING ? text : number(text,type); }
static NSString *text_value(id value,Kind type) {
    if (type==K_STRING) return [value isKindOfClass:NSString.class] ? value : nil;
    if (![value isKindOfClass:NSNumber.class]) return nil;
    if (type==K_BOOL) return [value boolValue] ? @"1" : @"0";
    NSNumber *checked=number([value stringValue],type);
    if (!checked) return nil;
    if (type==K_FLOAT) return [NSString stringWithFormat:@"%.9g",checked.floatValue];
    if (type==K_DOUBLE) return [NSString stringWithFormat:@"%.17g",checked.doubleValue];
    return checked.stringValue;
}
static BOOL write_object(NSDictionary *object,NSString *path) {
    NSData *bytes=[NSJSONSerialization dataWithJSONObject:object options:NSJSONWritingPrettyPrinted error:nil];
    if (!bytes) return NO;
    NSString *parent=path.stringByDeletingLastPathComponent;
    if (parent.length && ![NSFileManager.defaultManager createDirectoryAtPath:parent withIntermediateDirectories:YES attributes:nil error:nil]) return NO;
    return [bytes writeToFile:path options:NSDataWritingAtomic error:nil];
}
char *t7o_json_call(const char *operation,size_t argc,const char *const *argv) {
    @autoreleasepool {
        BOOL writing=!strcmp(operation,"json.write");
        if ((!writing && strcmp(operation,"json.read")) || argc<(writing ? 5u : 4u)) return NULL;
        NSString *path=argument(argc,argv,0),*section=argument(argc,argv,1),*key=argument(argc,argv,2);
        Kind type=kind(argument(argc,argv,3));
        if (!path.length || !key.length || type==K_INVALID) return NULL;
        /* Serializes the complete read/modify/write transaction. Unrelated
         * keys survive updates; malformed existing files are never replaced. */
        @synchronized(NSFileManager.defaultManager) {
            BOOL exists=[NSFileManager.defaultManager fileExistsAtPath:path];
            NSData *bytes=exists ? [NSData dataWithContentsOfFile:path] : nil;
            id parsed=bytes ? [NSJSONSerialization JSONObjectWithData:bytes options:NSJSONReadingMutableContainers error:nil] : nil;
            if (exists && ![parsed isKindOfClass:NSMutableDictionary.class]) return NULL;
            NSMutableDictionary *root=parsed ?: [NSMutableDictionary new];
            id nested=section.length ? root[section] : root;
            if (nested && ![nested isKindOfClass:NSMutableDictionary.class]) return NULL;
            NSMutableDictionary *object=nested ?: [NSMutableDictionary new];
            id value=object[key];
            BOOL readonly=argc<6 || ![@[@"0",@"false",@"off",@"no"] containsObject:argument(argc,argv,5).lowercaseString];
            if (writing || !value) {
                value=scalar(argument(argc,argv,4),type);
                if (!value) return NULL;
                if (writing || !readonly) {
                    object[key]=value;
                    if (section.length) root[section]=object;
                    if (!write_object(root,path)) return NULL;
                }
            }
            NSString *result=text_value(value,type);
            if (!result) return NULL;
            return strdup((writing ? @"1" : result).UTF8String);
        }
    }
}
