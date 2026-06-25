// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_FOCUS_ARBITER_H_
#define CHROME_BROWSER_AGENT_GATEWAY_FOCUS_ARBITER_H_

#include <string>

#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"

namespace agent_gateway {

// Process-wide, thread-safe arbiter of "who is allowed to foreground a tab" —
// the enforcement point for the requirements: agent work never steals focus,
// exactly one focus activity at a time, and agents never fight over the active
// tab.
//
// Agent tabs open inactive (NEW_BACKGROUND_TAB) and are driven via CDP without
// ever activating, so concurrent agents physically cannot fight over the active
// tab. The only remaining focus-stealing surface is explicit activation
// (POST /tabs/{id}/activate, SplitTab). The arbiter gates those: an agent may
// activate only if it currently holds the focus grant. The default owner is the
// user (empty owner string); any real user tab gesture resets ownership back to
// the user (driven from the AgentTabGrouper TabStripModelObserver), and a grant
// auto-expires when its owning run ends.
//
// Threading: MayActivate() runs on the gateway IO thread (inside RouteRequest);
// ResetToUser()/OnRunEnded() run on the UI thread. All state is lock-guarded.
class FocusArbiter {
 public:
  static FocusArbiter* Get();

  FocusArbiter(const FocusArbiter&) = delete;
  FocusArbiter& operator=(const FocusArbiter&) = delete;

  // True if |requester_agent_id| may activate a tab right now. An empty
  // requester (no X-Agent-Id == the user/UI) is always allowed; an agent is
  // allowed only if it currently owns the focus grant.
  bool MayActivate(const std::string& requester_agent_id);

  // Grant the foreground to |agent_id|, tied to |grant_conv_id| so the grant
  // auto-expires when that run ends. Called only from the user-driven
  // POST /focus route (which carries no X-Agent-Id).
  void SetOwner(const std::string& agent_id, const std::string& grant_conv_id);

  // Revoke any agent grant and return focus to the user. Called on any real
  // user tab gesture (gesture type != kNone in the tab-strip observer).
  void ResetToUser();

  // If |conv_id| is the run that holds the current grant, clear it. Called from
  // the run-teardown path (UnregisterActiveRun) so a finished foreground task
  // does not hold focus rights indefinitely.
  void OnRunEnded(const std::string& conv_id);

  // Current owner ("" == user). For diagnostics / status surfaces.
  std::string owner();

 private:
  FocusArbiter();
  ~FocusArbiter();

  base::Lock lock_;
  std::string owner_ GUARDED_BY(lock_);          // "" == user (default)
  std::string grant_conv_id_ GUARDED_BY(lock_);  // run that owns the grant
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_FOCUS_ARBITER_H_
