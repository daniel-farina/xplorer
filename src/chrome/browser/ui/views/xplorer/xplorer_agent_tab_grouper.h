// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"

class Browser;
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
  // One-shot launch seeder: reads the user-editable settings["bookmarks"] list
  // (materializing + persisting the built-in defaults on first run), then opens
  // each as a real background tab (stamped TabOwnership::bookmark_node_id) so
  // Reconcile() forms the native "Bookmarks" group. Guarded by |seeded_|.
  void SeedDefaultBookmarks();
  // Opens a background tab for every bookmark config whose bookmark_node_id is
  // not already present in the model (de-duped by id), stamping the id. Returns
  // true if any tab was opened. Shared by the seeder and the live-reload path.
  bool OpenMissingBookmarkTabs(Browser* browser,
                               const std::vector<base::DictValue>& configs);
  // Live-reload callback: fired when the user edits the bookmark list. Schedules
  // a deferred ApplyBookmarkConfig() (never mutates the model synchronously).
  void OnBookmarkConfigChanged();
  // Re-reads the bookmark config and reconciles open bookmark tabs: closes tabs
  // whose id was removed, opens tabs for new ids, then ScheduleReconcile().
  void ApplyBookmarkConfig();
  // Deferred drain of |closed_bookmark_ids_|: calls RemoveBookmarkConfig() for
  // each id a user manually closed, so the close sticks (the id is dropped from
  // the persisted config and won't be re-opened by a later ApplyBookmarkConfig
  // or a new window's seeder). Never mutates config inside the model
  // notification — staged + PostTask'd, mirroring the bookmark-reload path.
  void FlushClosedBookmarkConfigs();
  // Schedules a deferred SeedDefaultBookmarks() once the strip has a tab (so a
  // Browser exists). Called from the ctor and OnTabStripModelChanged; a no-op
  // once seeded or already scheduled.
  void MaybeScheduleSeed();

  const raw_ptr<TabStripModel> model_;
  bool reconciling_ = false;
  bool reconcile_scheduled_ = false;
  bool seeded_ = false;
  bool seed_scheduled_ = false;
  bool bookmark_reload_scheduled_ = false;
  // Bookmark ids the user manually closed, staged in OnTabStripModelChanged and
  // drained by FlushClosedBookmarkConfigs() (deferred — never mutate config in
  // the notification).
  std::vector<int64_t> closed_bookmark_ids_;
  bool bookmark_remove_scheduled_ = false;
  base::CallbackListSubscription bookmark_config_subscription_;
  base::WeakPtrFactory<AgentTabGrouper> weak_factory_{this};
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_AGENT_TAB_GROUPER_H_