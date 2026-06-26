// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"

class TabStripModel;

namespace xplorer {

// Keeps agent-owned tabs grouped in the vertical tab strip: ad-hoc agent tabs
// in "Agent tabs (N)", scheduled-task tabs in "Scheduled task tabs (N)" (rows
// hidden from the strip). Reasserts hidden scheduled rows on change.
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
  // Posts a coalesced Reconcile() to run after the current TabStripModel
  // operation finishes. Reconcile() mutates the model (AddToNewGroup etc.),
  // which must NOT run synchronously inside OnTabStripModelChanged — that
  // re-enters TabStripModel and trips its re-entrancy guard (CHECK !*guard_flag)
  // when, e.g., a background agent tab is activated. Deferring runs the grouping
  // outside the guarded section.
  void ScheduleReconcile();
  void Reconcile();
  // One-shot launch seeder: opens the hardcoded xAI default bookmarks as real
  // background tabs (stamped TabOwnership::bookmark_node_id) so Reconcile()
  // forms the native "Bookmarks" group. Guarded by |seeded_| to run once.
  void SeedDefaultBookmarks();
  // Schedules a deferred SeedDefaultBookmarks() once the strip has a tab (so a
  // Browser exists). Called from the ctor and OnTabStripModelChanged; a no-op
  // once seeded or already scheduled.
  void MaybeScheduleSeed();
  std::optional<tab_groups::TabGroupId> FindAgentGroup() const;
  tab_groups::TabGroupId EnsureAgentGroup(int count);

  const raw_ptr<TabStripModel> model_;
  bool reconciling_ = false;
  bool reconcile_scheduled_ = false;
  bool seeded_ = false;
  bool seed_scheduled_ = false;
  base::WeakPtrFactory<AgentTabGrouper> weak_factory_{this};
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_