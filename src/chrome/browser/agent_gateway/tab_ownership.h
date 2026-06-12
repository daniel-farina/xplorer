// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_TAB_OWNERSHIP_H_
#define CHROME_BROWSER_AGENT_GATEWAY_TAB_OWNERSHIP_H_

#include <string>

#include "base/supports_user_data.h"

namespace content {
class WebContents;
}

namespace agent_gateway {

// Per-tab metadata that an agent can own. Stored as user-data on the
// WebContents, so it travels with the tab for the tab's whole lifetime and
// survives tab reordering, unlike the session_id:index handle. This is how
// Aether answers "which agent owns this tab?".
class TabOwnership : public base::SupportsUserData::Data {
 public:
  static const void* const kKey;

  // Returns the ownership record for |wc|, creating an empty one if absent.
  static TabOwnership* GetOrCreate(content::WebContents* wc);
  // Returns the record for |wc| or nullptr if the tab is unowned.
  static TabOwnership* Get(content::WebContents* wc);

  // The agent that owns this tab (e.g. "researcher-1"). Empty == unowned/user.
  std::string owner;
  // Free-form label an agent can set to describe the tab's purpose.
  std::string label;
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_TAB_OWNERSHIP_H_
