// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"

class TabStripModel;

namespace xplorer {

// Keeps agent-owned tabs in a collapsible "Agent tabs (N)" group in the
// vertical tab strip, matching the Arc-style sidebar mockup.
class AgentTabGrouper : public TabStripModelObserver {
 public:
  explicit AgentTabGrouper(TabStripModel* model);
  AgentTabGrouper(const AgentTabGrouper&) = delete;
  AgentTabGrouper& operator=(const AgentTabGrouper&) = delete;
  ~AgentTabGrouper() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  void Reconcile();
  std::optional<tab_groups::TabGroupId> FindAgentGroup() const;
  tab_groups::TabGroupId EnsureAgentGroup(int count);

  const raw_ptr<TabStripModel> model_;
  bool reconciling_ = false;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_