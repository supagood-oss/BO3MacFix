#import <Foundation/Foundation.h>

@interface BO3SetupEngine : NSObject
+ (NSArray<NSURL *> *)findGames;
+ (NSURL *)gameBundleAt:(NSURL *)selection;
+ (NSString *)sha256:(NSURL *)file;
+ (BOOL)gameIsRunning;
+ (NSDictionary *)readiness:(NSURL *)game payload:(NSURL *)payload error:(NSError **)error;
+ (BOOL)install:(NSURL *)game payload:(NSURL *)payload error:(NSError **)error;
+ (BOOL)remove:(NSURL *)game error:(NSError **)error;
+ (BOOL)isInstalled:(NSURL *)game;
+ (BOOL)needsRecovery:(NSURL *)game;
+ (BOOL)recover:(NSURL *)game error:(NSError **)error;
+ (BOOL)verifyInstalled:(NSURL *)game payload:(NSURL *)payload error:(NSError **)error;
+ (NSDictionary *)diagnostics:(NSURL *)game payload:(NSURL *)payload;
@end
