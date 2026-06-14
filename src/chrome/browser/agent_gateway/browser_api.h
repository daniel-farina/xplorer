// Copyright 2026 The Aether Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_AGENT_GATEWAY_BROWSER_API_H_
#define CHROME_BROWSER_AGENT_GATEWAY_BROWSER_API_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"

namespace agent_gateway {

// Browser-level primitives for the Grok companion and MCP tools: bookmarks,
// history, tab management, groups, splits, and theme prefs. All methods run on
// the UI thread and invoke |callback| when done (history is async).
class BrowserApi {
 public:
  using DictCallback = base::OnceCallback<void(base::DictValue)>;

  static void ListBookmarks(DictCallback callback);
  static void AddBookmark(const std::string& url,
                          const std::string& title,
                          const std::string& parent_id,
                          DictCallback callback);
  static void RemoveBookmark(const std::string& id, DictCallback callback);
  static void QueryHistory(const std::string& query,
                           int limit,
                           DictCallback callback);
  static void ActivateTab(const std::string& tab_id, DictCallback callback);
  static void CloseTab(const std::string& tab_id, DictCallback callback);
  static void GroupTabs(const std::vector<std::string>& tab_ids,
                        const std::string& title,
                        DictCallback callback);
  // Heuristic tab grouping (Grok, Xplorer, News, …) — one native call.
  static void OrganizeTabs(DictCallback callback);
  static void SplitTab(const std::string& tab_id,
                       const std::string& layout,
                       DictCallback callback);
  static void GetTheme(DictCallback callback);
  static void SetTheme(const std::string& color_scheme,
                       DictCallback callback);
};

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_BROWSER_API_H_