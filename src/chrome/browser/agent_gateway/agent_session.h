// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_AGENT_SESSION_H_
#define CHROME_BROWSER_AGENT_GATEWAY_AGENT_SESSION_H_

#include <map>
#include <string>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

namespace agent_gateway {

// One attached agent. Wraps an in-process CDP client against a tab's
// DevToolsAgentHost and implements the high-level primitives on top of it.
// All methods are async; results are returned as base::DictValue via
// |ResultCallback| on the UI thread.
class AgentSession : public content::DevToolsAgentHostClient,
                     public content::WebContentsObserver {
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

  // content::WebContentsObserver: resolve a pending navigation when the tab
  // finishes loading. This is a browser-process signal that survives the
  // renderer process swaps that drop CDP Page.loadEventFired, so it is the
  // primary navigation-complete signal (loadEventFired + the timer are backstops).
  void DidStopLoading() override;

 private:
  // Sends a raw CDP command; |cb| fires when the matching id returns.
  void SendCommand(const std::string& method,
                   base::DictValue params,
                   ResultCallback cb);

  // Resolves a still-pending navigation (|load_waiter_|) with |result|, posted
  // as a fresh task. Running |load_waiter_| frees this AgentSession (the gateway
  // reply it carries owns the unique_ptr), so it must NEVER be run synchronously
  // from a timer fire / host callback — that would destroy `this` mid-callback.
  void ResolveNavPosted(base::DictValue result);
  // Page.navigate command ack: fail fast on a protocol/URL error instead of
  // waiting out the navigation timeout.
  void OnNavigateAck(base::DictValue result);
  // Navigation timeout: Page.loadEventFired can be lost (e.g. a cross-process
  // navigation re-homes the renderer), which would otherwise hang the caller.
  void OnNavTimeout();

  scoped_refptr<content::DevToolsAgentHost> host_;
  base::WeakPtr<content::WebContents> web_contents_;
  int next_id_ = 1;
  std::map<int, ResultCallback> pending_;
  ResultCallback load_waiter_;
  base::OneShotTimer nav_timeout_;
  base::WeakPtrFactory<AgentSession> weak_factory_{this};
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_AGENT_SESSION_H_
