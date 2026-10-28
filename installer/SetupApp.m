#import <AppKit/AppKit.h>
#import "SetupEngine.h"
#import "SetupDependencies.h"

@interface SetupApp : NSObject <NSApplicationDelegate>
@property NSWindow *window;
@property NSTextField *message;
@property NSTextField *location;
@property NSButton *installButton;
@property NSButton *playButton;
@property NSButton *removeButton;
@property NSButton *chooseButton;
@property NSButton *reportButton;
@property NSURL *game;
@property NSURL *payload;
@property NSTask *gameTask;
@property BOOL busy;
@end

@implementation SetupApp
- (void)alert:(NSString *)title detail:(NSString *)detail {
    NSAlert *alert=[NSAlert new]; alert.messageText=title;
    alert.informativeText=detail ?: @"Please try again."; [alert addButtonWithTitle:@"OK"];
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}
- (NSButton *)button:(NSString *)title action:(SEL)action x:(CGFloat)x width:(CGFloat)width {
    NSButton *button=[NSButton buttonWithTitle:title target:self action:action];
    button.bezelStyle=NSBezelStyleRounded; button.frame=NSMakeRect(x,30,width,36);
    [self.window.contentView addSubview:button]; return button;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    self.payload=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"payload"];
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,680,320)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"Black Ops III — BO3MacFix"; [self.window center];
    NSTextField *title=[NSTextField labelWithString:@"Black Ops III — AAE / UEM compatibility test"];
    title.font=[NSFont boldSystemFontOfSize:21]; title.frame=NSMakeRect(28,250,630,35);
    [self.window.contentView addSubview:title];
    self.message=[NSTextField wrappingLabelWithString:@"Looking for your game…"];
    self.message.font=[NSFont systemFontOfSize:15]; self.message.frame=NSMakeRect(28,135,624,100);
    [self.window.contentView addSubview:self.message];
    self.location=[NSTextField labelWithString:@""]; self.location.frame=NSMakeRect(28,90,624,35);
    self.location.lineBreakMode=NSLineBreakByTruncatingMiddle; self.location.textColor=NSColor.secondaryLabelColor;
    [self.window.contentView addSubview:self.location];
    self.chooseButton=[self button:@"Choose Game Folder…" action:@selector(choose:) x:24 width:165];
    self.reportButton=[self button:@"Save Report…" action:@selector(report:) x:195 width:120];
    self.removeButton=[self button:@"Remove" action:@selector(remove:) x:338 width:90];
    self.installButton=[self button:@"Install" action:@selector(install:) x:436 width:100];
    self.playButton=[self button:@"Play" action:@selector(play:) x:544 width:100];
    self.playButton.keyEquivalent=@"\r";
    [self.window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    NSString *saved=[NSUserDefaults.standardUserDefaults stringForKey:@"gameBundle"];
    self.game=[BO3SetupEngine gameBundleAt:saved ? [NSURL fileURLWithPath:saved] : nil];
    if (!self.game) {
        NSArray *found=[BO3SetupEngine findGames];
        if (found.count==1) self.game=found.firstObject;
        else if (found.count>1) self.message.stringValue=@"More than one copy of the game was found. Click Choose Game Folder to select one.";
    }
    [self refresh];
}
- (void)refresh {
    self.location.stringValue=self.game.path ?: @"No game folder selected";
    BOOL installed=self.game && [BO3SetupEngine isInstalled:self.game];
    self.chooseButton.enabled=!self.busy; self.reportButton.enabled=!self.busy;
    self.removeButton.enabled=!self.busy && installed;
    self.playButton.enabled=!self.busy && installed;
    self.installButton.enabled=!self.busy && self.game!=nil;
    self.installButton.title=installed ? @"Update" : @"Install";
    [self.window standardWindowButton:NSWindowCloseButton].enabled=!self.busy;
    if (self.busy) return;
    if (!self.game) { self.message.stringValue=@"Your game wasn't found automatically. Click Choose Game Folder and select Black Ops III. Nothing will be changed until you click Install."; return; }
    if ([BO3SetupEngine needsRecovery:self.game]) {
        self.installButton.title=@"Repair";self.installButton.enabled=YES;
        self.playButton.enabled=NO;self.removeButton.enabled=NO;
        self.message.stringValue=@"Setup was interrupted. Click Repair to recover your files, then install or play again. Your saves and Workshop downloads will be kept.";
        return;
    }
    NSError *error=nil;
    if (![BO3SetupEngine readiness:self.game payload:self.payload error:&error]) {
        self.message.stringValue=error.localizedDescription;
        self.installButton.enabled=NO; self.playButton.enabled=NO; return;
    }
    // Verify downloaded helpers once when Play is requested, off the UI thread.
    self.message.stringValue=installed ? @"Installed. Keep Steam open, then click Play here to test.\nAAE / UEM co-op compatibility is still being tested." :
        @"Game found. Click Install to download and set up the helpers (about 340 MB). Then use Play here.\nThis is an experimental build for the first Mac co-op test.";
}
- (void)choose:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel]; panel.canChooseDirectories=YES; panel.canChooseFiles=YES;
    panel.allowedFileTypes=@[@"app"];
    panel.treatsFilePackagesAsDirectories=NO; panel.prompt=@"Choose Game";
    panel.message=@"Select CoDBlkOps3.app, or the folder containing it. In Steam: Manage → Browse local files.";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response!=NSModalResponseOK) return;
        NSURL *game=[BO3SetupEngine gameBundleAt:panel.URL];
        if (!game) { [self alert:@"Game not found" detail:@"Choose the installed Mac copy of Black Ops III."]; return; }
        self.game=game; [NSUserDefaults.standardUserDefaults setObject:game.path forKey:@"gameBundle"];
        [self refresh];
    }];
}
- (void)install:(id)sender {
    BOOL recovery=[BO3SetupEngine needsRecovery:self.game];
    self.busy=YES; self.message.stringValue=recovery ? @"Recovering your files…" : @"Installing… Please keep this window open."; [self refresh];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSError *error=nil; BOOL ok=NO;
        if (recovery) ok=[BO3SetupEngine recover:self.game error:&error];
        else {
            NSURL *prepared=[BO3SetupDependencies prepare:self.payload progress:^(NSString *message) {
                dispatch_async(dispatch_get_main_queue(), ^{ self.message.stringValue=message; });
            } error:&error];
            if (prepared) {
                dispatch_async(dispatch_get_main_queue(), ^{ self.message.stringValue=@"Installing… Please keep this window open."; });
                ok=[BO3SetupEngine install:self.game payload:prepared error:&error];
                if (![prepared isEqual:self.payload]) [NSFileManager.defaultManager removeItemAtURL:prepared.URLByDeletingLastPathComponent error:nil];
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            self.busy=NO; [self refresh];
            if (!ok) [self alert:@"Setup could not finish" detail:error.localizedDescription];
        });
    });
}
- (void)remove:(id)sender {
    NSAlert *alert=[NSAlert new]; alert.messageText=@"Remove this installation?";
    alert.informativeText=@"Setup will restore the files it replaced. Your game, saves, Workshop downloads and Wine data will be kept.";
    [alert addButtonWithTitle:@"Remove"]; [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response!=NSAlertFirstButtonReturn) return;
        self.busy=YES; self.message.stringValue=@"Restoring your previous files…"; [self refresh];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
            NSError *error=nil; BOOL ok=[BO3SetupEngine remove:self.game error:&error];
            dispatch_async(dispatch_get_main_queue(), ^{
                self.busy=NO; [self refresh];
                if (!ok) [self alert:@"Removal could not finish" detail:error.localizedDescription];
            });
        });
    }];
}
- (void)play:(id)sender {
    if (self.busy) return;
    if ([BO3SetupEngine gameIsRunning]) { [self alert:@"Black Ops III is already running" detail:@"Switch to the game, or close it before launching again."]; return; }
    self.busy=YES; self.message.stringValue=@"Checking your installation… Please wait."; [self refresh];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSError *error=nil;
        BOOL ok=[BO3SetupEngine verifyInstalled:self.game payload:self.payload error:&error];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.busy=NO; [self refresh];
            if (!ok) [self alert:@"The installation needs attention" detail:error.localizedDescription];
            else [self launchGame];
        });
    });
}
- (void)launchGame {
    if ([BO3SetupEngine gameIsRunning]) { [self alert:@"Black Ops III is already running" detail:@"Switch to the game, or close it before launching again."]; return; }
    NSError *error=nil;
    if (![BO3SetupDependencies canRunIntelCode]) {
        self.busy=YES;self.message.stringValue=@"macOS needs Rosetta to run the game. Follow Apple's Install prompt. Setup will continue when it is ready.";[self refresh];
        NSWorkspaceOpenConfiguration *configuration=[NSWorkspaceOpenConfiguration configuration];
        configuration.createsNewApplicationInstance=YES;
        NSURL *helper=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Rosetta Check.app"];
        [NSWorkspace.sharedWorkspace openApplicationAtURL:helper configuration:configuration completionHandler:^(NSRunningApplication *application,NSError *launchError) {
            dispatch_async(dispatch_get_main_queue(), ^{
                self.busy=NO;[self refresh];
                if (launchError || ![BO3SetupDependencies canRunIntelCode])
                    [self alert:@"Rosetta is needed" detail:@"Complete Apple's Rosetta installation, then click Play again. Rosetta lets your Mac run the Intel version of Black Ops III."];
                else [self launchGame];
            });
        }];
        return;
    }
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"steam://open/main"]];
    NSURL *root=[self.game.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"BO3MacFix"];
    self.gameTask=[NSTask new]; self.gameTask.executableURL=[root URLByAppendingPathComponent:@"AppBundleExe"];
    self.gameTask.arguments=@[@"--game-dir",[self.game URLByAppendingPathComponent:@"Contents/MacOS"].path];
    self.gameTask.standardOutput=[NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
    self.gameTask.standardError=[NSFileHandle fileHandleForWritingAtPath:@"/dev/null"];
    if (![self.gameTask launchAndReturnError:&error]) [self alert:@"The game couldn't start" detail:error.localizedDescription];
}
- (void)report:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel]; panel.nameFieldStringValue=@"BO3MacFix Report.json";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response!=NSModalResponseOK) return;
        NSError *error=nil;
        NSData *data=[NSJSONSerialization dataWithJSONObject:[BO3SetupEngine diagnostics:self.game payload:self.payload]
            options:NSJSONWritingPrettyPrinted error:&error];
        if (!data || ![data writeToURL:panel.URL options:NSDataWritingAtomic error:&error])
            [self alert:@"The report couldn't be saved" detail:error.localizedDescription];
    }];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    if (self.busy) { [self alert:@"Setup is still working" detail:@"Please wait for it to finish before closing this window."]; return NSTerminateCancel; }
    return NSTerminateNow;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return YES; }
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        NSApplication *app=NSApplication.sharedApplication;
        SetupApp *delegate=[SetupApp new]; app.delegate=delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
