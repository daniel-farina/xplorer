// Copyright 2026 The Xplorer Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

#import "chrome/browser/xplorer_sparkle_updater.h"

// Sparkle.framework is now a LINKED dependency (frameworks += "Sparkle.framework"
// + framework_dirs = //third_party/sparkle in chrome/browser/BUILD.gn, with the
// @rpath added in chrome/BUILD.gn's chrome_framework ldflags). Runtime-loading
// via [NSBundle load] / NSClassFromString fails under the hardened runtime, so we
// link directly and use the real <Sparkle/Sparkle.h> API — the way Vivaldi/Brave
// embed Sparkle.

// Held for the lifetime of the process. Under ARC (Chromium enables ARC for all
// Apple targets) this static strong reference keeps the controller alive so its
// scheduled update checks keep firing.
static SPUStandardUpdaterController* g_updater = nil;

void XplorerStartSparkleUpdater() {
  if (g_updater) {
    return;
  }

  // The controller targets the app's main bundle, starts the updater, shows
  // Sparkle's native update UI, and self-schedules checks per the SU* Info.plist
  // keys (SUFeedURL / SUPublicEDKey / SUScheduledCheckInterval).
  g_updater = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
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
