// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_AGENT_SESSION_H_
#define CHROME_BROWSER_AGENT_GATEWAY_AGENT_SESSION_H_

#include <map>
#include <string>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host_client.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

namespace agent_gateway {

// One attached agent. Wraps an in-process CDP client against a tab's
// DevToolsAgentHost and implements the high-level primitives on top of it.
// All methods are async; results are returned as base::DictValue via
// |ResultCallback| on the UI thread.
class AgentSession : public content::DevToolsAgentHostClient {
 public:
  using ResultCallback = base::OnceCallback<void(base::DictValue)>;

  explicit AgentSession(content::WebContents* web_contents);
  ~AgentSession() override;

  // High-level primitives. Each maps to one or a few CDP commands but
  // presents a single async result to the caller.
  void Navigate(const std::string& url, ResultCallback cb);
  void ExtractText(ResultCallback cb);          // Runtime.evaluate(reader js)
  void AXTree(ResultCallback cb);               // Accessibility.getFullAXTree
  void Screenshot(ResultCallback cb);           // Page.captureScreenshot
  void Click(const std::string& selector, ResultCallback cb);
  void Type(const std::string& selector,
            const std::string& text,
            ResultCallback cb);
  // Sends a real key press (keyDown+keyUp) to the focused element. Robust way
  // to drive autocompletes/menus: e.g. "ArrowDown" then "Enter". Supports the
  // common navigation/commit keys by name.
  void Press(const std::string& key, ResultCallback cb);
  void Eval(const std::string& expression, ResultCallback cb);

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* host) override;

 private:
  // Sends a raw CDP command; |cb| fires when the matching id returns.
  void SendCommand(const std::string& method,
                   base::DictValue params,
                   ResultCallback cb);
  void WaitForLoad(ResultCallback cb);

  scoped_refptr<content::DevToolsAgentHost> host_;
  base::WeakPtr<content::WebContents> web_contents_;
  // Held while a screenshot is in flight so the renderer keeps producing
  // compositor frames even when the tab/window is occluded or backgrounded.
  base::ScopedClosureRunner capture_hold_;
  int next_id_ = 1;
  std::map<int, ResultCallback> pending_;
  ResultCallback load_waiter_;
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_AGENT_SESSION_H_
