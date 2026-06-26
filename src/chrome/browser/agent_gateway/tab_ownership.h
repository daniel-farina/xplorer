// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_TAB_OWNERSHIP_H_
#define CHROME_BROWSER_AGENT_GATEWAY_TAB_OWNERSHIP_H_

#include <string>

#include "base/supports_user_data.h"

namespace content {
class WebContents;
}

namespace agent_gateway {

class TabOwnership;

// True when a tab belongs to a scheduled background task (not ad-hoc agent chat).
// Requires both task_id and owner "schedule:<job_id>" so leaked task_id alone
// cannot pull normal chat tabs into the scheduled group.
bool IsScheduledTaskTab(const TabOwnership* own);

// Per-tab metadata that an agent can own. Stored as user-data on the
// WebContents, so it travels with the tab for the tab's whole lifetime and
// survives tab reordering, unlike the session_id:index handle. This is how
// Xplorer answers "which agent owns this tab?".
class TabOwnership : public base::SupportsUserData::Data {
 public:
  static const void* const kKey;

  TabOwnership();
  ~TabOwnership() override;

  // Returns the ownership record for |wc|, creating an empty one if absent.
  static TabOwnership* GetOrCreate(content::WebContents* wc);
  // Returns the record for |wc| or nullptr if the tab is unowned.
  static TabOwnership* Get(content::WebContents* wc);

  // The agent that owns this tab (e.g. "researcher-1"). Empty == unowned/user.
  std::string owner;
  // Free-form label an agent can set to describe the tab's purpose.
  std::string label;
  // The model driving this tab (e.g. "Grok"), from X-Agent-Model.
  std::string model;
  std::string last_action;
  // Which scheduled/user task owns this tab ("" == ad-hoc). Lets GET /tabs
  // answer "which tabs belong to task X" and the UI offer per-task focus.
  std::string task_id;
  // True if this tab was opened as a background agent tab (inactive/hidden);
  // false for user/foreground tabs. Agent tabs default to background so they
  // never steal the user's focus.
  bool background = false;

  // Marks a tab opened from the sidebar Bookmarks group. Non-zero ==
  // member of the native "Bookmarks" tab group; used for grouping + the
  // sidebar's active-row highlight.
  int64_t bookmark_node_id = 0;

  // Per-tab activity counters — what the in-tab HUD shows, so each tab
  // reflects only the agent controlling it (not a global blend).
  int64_t requests = 0;
  int64_t bytes_in = 0;
  int64_t bytes_out = 0;
  int64_t navigations = 0;
  int64_t clicks = 0;
  int64_t types = 0;
  int64_t presses = 0;
  int64_t reads = 0;
  int64_t screenshots = 0;
  int64_t evals = 0;
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_TAB_OWNERSHIP_H_
