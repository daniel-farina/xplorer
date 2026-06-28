// Copyright 2026 The Xplorer Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_XPLORER_SPARKLE_UPDATER_H_
#define CHROME_BROWSER_XPLORER_SPARKLE_UPDATER_H_

// Starts the self-contained Sparkle 2.x auto-updater on macOS.
//
// Sparkle.framework is LINKED into the build (frameworks += "Sparkle.framework"
// in chrome/browser/BUILD.gn; @rpath added in chrome/BUILD.gn), so this just
// instantiates SPUStandardUpdaterController (which checks for updates on its own
// schedule, shows Sparkle's native update dialog, downloads, installs, and
// relaunches) and inserts a "Check for Updates…" item into the app menu.
// (Runtime-loading via NSBundle failed under the hardened runtime.)
//
// Declared as plain C++ (no Objective-C in this header) so it can be called
// from app_controller_mac.mm without forcing ObjC++ on its includers. The
// implementation is Objective-C++ in xplorer_sparkle_updater.mm.
//
// Must be called on the main thread after the main menu exists. It is a no-op
// if called more than once.
void XplorerStartSparkleUpdater();

#endif  // CHROME_BROWSER_XPLORER_SPARKLE_UPDATER_H_
