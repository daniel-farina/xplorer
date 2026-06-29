// Copyright 2026 The Xplorer Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_XPLORER_SPARKLE_UPDATER_H_
#define CHROME_BROWSER_XPLORER_SPARKLE_UPDATER_H_

#include <string>

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

// Update controls exposed to the companion Settings "Updates" pane (the agent
// gateway bridges HTTP requests to these). All must be called on the main
// thread, and are safe no-ops before XplorerStartSparkleUpdater() has run.

// Whether the updater has been started (Sparkle is available this session).
bool XplorerSparkleAvailable();

// Whether Sparkle's automatic (scheduled) update checks are currently enabled.
bool XplorerSparkleAutoCheckEnabled();

// Enable or disable Sparkle's automatic (scheduled) update checks. This is the
// user's persistent preference (Sparkle stores it in the app's user defaults).
void XplorerSparkleSetAutoCheck(bool enabled);

// Trigger a user-visible update check now (identical to the "Check for Updates…"
// menu item — shows Sparkle's native dialog).
void XplorerSparkleCheckNow();

// The app's user-facing version string (CFBundleShortVersionString, e.g.
// "0.8.9"), for display in the Settings "Updates" pane.
std::string XplorerSparkleCurrentVersion();

#endif  // CHROME_BROWSER_XPLORER_SPARKLE_UPDATER_H_
