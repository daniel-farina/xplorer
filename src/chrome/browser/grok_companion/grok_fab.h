// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_GROK_COMPANION_GROK_FAB_H_
#define CHROME_BROWSER_GROK_COMPANION_GROK_FAB_H_

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace grok_companion {

// Injects a floating Grok button on browsed pages (summarize + chat).
void RegisterGrokFab(BrowserWindowInterface* browser);

// Attaches the per-tab injector (idempotent). Called from TabHelpers.
void AttachGrokFabInjector(content::WebContents* contents);

}  // namespace grok_companion

#endif  // CHROME_BROWSER_GROK_COMPANION_GROK_FAB_H_