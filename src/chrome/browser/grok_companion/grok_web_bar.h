// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_GROK_COMPANION_GROK_WEB_BAR_H_
#define CHROME_BROWSER_GROK_COMPANION_GROK_WEB_BAR_H_

class BrowserWindowInterface;

namespace grok_companion {

// Injects the Grok Build / Grok Web toggle bar on grok.com tabs.
void RegisterGrokWebBar(BrowserWindowInterface* browser);

}  // namespace grok_companion

#endif  // CHROME_BROWSER_GROK_COMPANION_GROK_WEB_BAR_H_