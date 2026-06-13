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

GURL GetCompanionURL();
GURL GetSearchURL();

// Open the native Grok Search page in the active tab (NTP-style homepage).
void OpenGrokSearchPage(BrowserWindowInterface* browser);

// Toggle the Grok side panel (chat UI). Called from the toolbar Grok button.
void ToggleGrokSidePanel(BrowserWindowInterface* browser);

// Register the Grok side panel entry on the global side panel registry.
void RegisterGrokSidePanel(BrowserWindowInterface* browser);

}  // namespace grok_companion

#endif  // CHROME_BROWSER_GROK_COMPANION_GROK_COMPANION_UTIL_H_