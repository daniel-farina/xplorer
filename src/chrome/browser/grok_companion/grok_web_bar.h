// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_GROK_COMPANION_GROK_WEB_BAR_H_
#define CHROME_BROWSER_GROK_COMPANION_GROK_WEB_BAR_H_

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace grok_companion {

// Injects the Grok navigation toolbar on grok.com, grokipedia.com, x.com, etc.
void RegisterGrokWebBar(BrowserWindowInterface* browser);

// Attaches the per-tab injector (idempotent). Called from TabHelpers.
void AttachGrokWebBarInjector(content::WebContents* contents);

}  // namespace grok_companion

#endif  // CHROME_BROWSER_GROK_COMPANION_GROK_WEB_BAR_H_