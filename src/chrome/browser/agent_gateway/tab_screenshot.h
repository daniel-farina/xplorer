// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_TAB_SCREENSHOT_H_
#define CHROME_BROWSER_AGENT_GATEWAY_TAB_SCREENSHOT_H_

#include "base/functional/callback.h"
#include "base/values.h"

namespace content {
class WebContents;
}

namespace agent_gateway {

// Compositor screenshot of a tab. Does not attach DevTools (unlike
// AgentSession). Returns {"data": "<base64 png>"} or {"error": "..."}.
void CaptureTabScreenshot(
    content::WebContents* web_contents,
    base::OnceCallback<void(base::DictValue)> callback);

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_TAB_SCREENSHOT_H_