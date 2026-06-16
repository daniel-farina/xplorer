// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_GROK_COMPANION_LAUNCHER_H_
#define CHROME_BROWSER_AGENT_GATEWAY_GROK_COMPANION_LAUNCHER_H_

namespace agent_gateway {

// Writes ~/.xplorer/companion.json pointing at the native in-browser Grok UI
// served by AgentGateway (same port as /tabs, /bookmarks, etc.).
void WriteCompanionDiscovery(int gateway_port);

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_GROK_COMPANION_LAUNCHER_H_