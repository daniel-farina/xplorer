// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_
#define CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_

#include "url/gurl.h"

class BrowserWindowInterface;

namespace grok_companion {

inline constexpr char kCompanionHost[] = "127.0.0.1";
// Grok UI is served natively by AgentGateway (default 9334).
inline constexpr int kCompanionPort = 9334;
inline constexpr char kCompanionPath[] = "/";
inline constexpr char kSearchPath[] = "/search";
inline constexpr char kGrokWebHomeURL[] = "https://grok.com/";
inline constexpr char kSearchHomeBuild[] = "build";
inline constexpr char kSearchHomeWeb[] = "web";

GURL GetCompanionURL();
GURL GetSearchURL();

// User preference: native Grok Build UI vs grok.com (stored in grok_settings.json).
std::string GetSearchHomeMode();
void SetSearchHomeMode(const std::string& mode);

// NTP / omnibox Grok chip destination based on search_home preference.
GURL GetDefaultSearchHomeURL();

// Open Grok Search home (build or web) in the active tab.
void OpenGrokSearchPage(BrowserWindowInterface* browser);

// Toggle the Grok side panel (chat UI). Called from the toolbar Grok button.
void ToggleGrokSidePanel(BrowserWindowInterface* browser);

// Register the Grok side panel entry on the global side panel registry.
void RegisterGrokSidePanel(BrowserWindowInterface* browser);

}  // namespace grok_companion

#endif  // CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_