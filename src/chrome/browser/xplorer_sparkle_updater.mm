// Copyright 2026 The Xplorer Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#import "base/apple/bundle_locations.h"
#import "chrome/browser/xplorer_sparkle_updater.h"

// Minimal forward declaration of the slice of Sparkle's public API we use.
// Sparkle.framework is runtime-loaded (no -framework link, no rpath, no GN
// framework dependency), so we declare only what we call and resolve the class
// dynamically with NSClassFromString().
@interface SPUStandardUpdaterController : NSObject
- (instancetype)initWithStartingUpdater:(BOOL)starting
                        updaterDelegate:(id)ud
                     userDriverDelegate:(id)udd;
- (void)checkForUpdates:(id)sender;
@end

// Held for the lifetime of the process. Under ARC (Chromium enables ARC for all
// Apple targets) this static strong reference keeps the controller alive so its
// scheduled update checks keep firing.
static SPUStandardUpdaterController* g_updater = nil;

void XplorerStartSparkleUpdater() {
  if (g_updater) {
    return;
  }

  // base::apple::FrameworkBundle() resolves to the Chrome/Xplorer framework
  // bundle (.../Contents/Frameworks/Xplorer Framework.framework). Its parent
  // directory is Contents/Frameworks, where build.sh / release_arch.sh stage
  // Sparkle.framework before signing.
  NSURL* fw = [[base::apple::FrameworkBundle().bundleURL
      URLByDeletingLastPathComponent]
      URLByAppendingPathComponent:@"Sparkle.framework"];
  NSBundle* b = [NSBundle bundleWithURL:fw];
  if (![b load]) {
    NSLog(@"Xplorer: Sparkle failed to load");
    return;
  }

  Class cls = NSClassFromString(@"SPUStandardUpdaterController");
  if (!cls) {
    NSLog(@"Xplorer: SPUStandardUpdaterController missing");
    return;
  }

  g_updater = [[cls alloc] initWithStartingUpdater:YES
                                   updaterDelegate:nil
                                userDriverDelegate:nil];

  // Add a "Check for Updates…" item just under the app's "About" item.
  NSMenu* appMenu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
  if (appMenu) {
    NSMenuItem* it =
        [[NSMenuItem alloc] initWithTitle:@"Check for Updates…"
                                   action:@selector(checkForUpdates:)
                            keyEquivalent:@""];
    it.target = g_updater;
    [appMenu insertItem:it atIndex:1];
  }
}
