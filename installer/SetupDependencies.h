#import <Foundation/Foundation.h>

// Called on a worker queue. Downloads only after the player presses Install.
@interface BO3SetupDependencies : NSObject
+ (NSString *)runtimeArchitecture;
+ (BOOL)canRunIntelCode;
+ (NSURL *)prepare:(NSURL *)payload progress:(void (^)(NSString *))progress error:(NSError **)error;
// Shared by the downloader and the synthetic filesystem fixtures.
+ (BOOL)copyTree:(NSURL *)source to:(NSURL *)destination boundary:(NSURL *)boundary error:(NSError **)error;
@end
