// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_agent_tab_grouper.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/agent_gateway/focus_arbiter.h"
#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/xplorer/xplorer_scheduled_task_tabs.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace xplorer {

namespace {
// Per-agent groups are titled "Agent: <owner> (N)". The legacy single
// catch-all "Agent tabs (N)" group is gone (each agent now owns its own group).
constexpr char16_t kAgentGroupTitlePrefix[] = u"Agent: ";
constexpr char16_t kScheduledGroupPrefix[] = u"Scheduled task tabs";
constexpr char16_t kBookmarksGroupPrefix[] = u"Bookmarks";

// Stable color palette for per-agent tab groups. Excludes kBlue (reserved for
// the Scheduled task tabs group) and kYellow (Bookmarks) so an agent group can
// never be visually confused with the two other managed groups. Each agent's
// color is picked by a stable hash of its owner string, so it keeps the same
// color as other agents come and go (the hash doesn't depend on group order).
constexpr tab_groups::TabGroupColorId kAgentColorCycle[] = {
    tab_groups::TabGroupColorId::kGrey,
    tab_groups::TabGroupColorId::kRed,
    tab_groups::TabGroupColorId::kGreen,
    tab_groups::TabGroupColorId::kPink,
    tab_groups::TabGroupColorId::kPurple,
    tab_groups::TabGroupColorId::kCyan,
    tab_groups::TabGroupColorId::kOrange,
};

// Hardcoded xAI default bookmarks, seeded as always-open tabs in the native
// "Bookmarks" group at launch. Gateway-relative hrefs ("/apps",
// "/switch-home?...") resolve against the companion base; absolute https hrefs
// are used as-is. The synthetic id stamped on each tab is its array index + 1.
constexpr struct {
  const char* label;
  const char* href;
} kDefaultBookmarks[] = {
    {"X Chat", "https://x.com/i/chat"},
    {"Grok Build", "/apps"},
    {"Grok Web", "/switch-home?mode=web"},
    {"Imagine", "https://grok.com/imagine"},
    {"Groki", "/switch-home?mode=wiki"},
    {"x.com", "https://x.com/"},
    {"Grok on X", "https://x.com/i/grok"},
};

// Resolves a bookmark href to a navigable URL: gateway-relative paths (leading
// '/') resolve against the companion base; everything else is taken as-is.
GURL ResolveBookmarkHref(const char* href) {
  std::string h(href);
  if (!h.empty() && h.front() == '/') {
    return grok_companion::GetCompanionURL().Resolve(h);
  }
  return GURL(h);
}

bool IsScheduledTaskWebContents(content::WebContents* wc) {
  return agent_gateway::IsScheduledTaskTab(agent_gateway::TabOwnership::Get(wc));
}

bool IsAdHocAgentTab(content::WebContents* wc) {
  agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
  return own && !own->owner.empty() &&
         !agent_gateway::IsScheduledTaskTab(own);
}

std::u16string GroupTitle(std::u16string_view prefix, int count) {
  return std::u16string(prefix) + u" (" + base::NumberToString16(count) +
         u")";
}

std::vector<int> IndicesInGroup(TabStripModel* model,
                                tab_groups::TabGroupId group) {
  std::vector<int> indices;
  for (int i = 0; i < model->count(); ++i) {
    std::optional<tab_groups::TabGroupId> tab_group =
        model->GetTabGroupForTab(i);
    if (tab_group.has_value() && tab_group.value() == group) {
      indices.push_back(i);
    }
  }
  return indices;
}

std::optional<tab_groups::TabGroupId> FindGroupWithPrefix(
    TabStripModel* model,
    std::u16string_view prefix) {
  if (!model->group_model()) {
    return std::nullopt;
  }
  // A managed group's title is exactly "<prefix> (N)". Match on the prefix
  // FOLLOWED BY the " (" count suffix so e.g. prefix "Agent: foo" does not also
  // match a sibling group "Agent: foobar (5)". (Reserved prefixes still match:
  // "Bookmarks (7)" starts with "Bookmarks (", "Scheduled task tabs (1)" with
  // "Scheduled task tabs (".)
  const std::u16string needle = std::u16string(prefix) + u" (";
  for (const tab_groups::TabGroupId& id :
       model->group_model()->ListTabGroups()) {
    TabGroup* group = model->group_model()->GetTabGroup(id);
    if (group && group->visual_data() &&
        base::StartsWith(group->visual_data()->title(), needle)) {
      return id;
    }
  }
  return std::nullopt;
}

// The tab-group-sync service for |browser|'s profile, or nullptr. Symbols
// resolve at the final link (sibling files in //chrome/browser/ui already use
// this factory) — no new GN dep, mirroring the existing grok_companion pattern.
tab_groups::TabGroupSyncService* SyncServiceFor(Browser* browser) {
  return browser ? tab_groups::TabGroupSyncServiceFactory::GetForProfile(
                       browser->profile())
                 : nullptr;
}

tab_groups::TabGroupId EnsureGroup(TabStripModel* model,
                                   [[maybe_unused]] tab_groups::TabGroupSyncService* sync,
                                   std::u16string_view prefix,
                                   const std::vector<int>& indices,
                                   tab_groups::TabGroupColorId color) {
  std::optional<tab_groups::TabGroupId> existing = FindGroupWithPrefix(model, prefix);
  tab_groups::TabGroupVisualData visual(
      GroupTitle(prefix, static_cast<int>(indices.size())), color);
  if (existing.has_value()) {
    // Only fire ChangeTabGroupVisuals when the visuals actually changed:
    // SetVisualData has no equality check and fans out OnTabGroupChanged
    // (kVisualsChanged) to every observer (incl. the sync listener) plus a heap
    // alloc on every call. Reconcile() runs on every tab-strip change, so the
    // title/color are usually identical — skip the no-op churn.
    TabGroup* group = model->group_model()
                          ? model->group_model()->GetTabGroup(existing.value())
                          : nullptr;
    if (!group || !group->visual_data() || !(*group->visual_data() == visual)) {
      model->ChangeTabGroupVisuals(existing.value(), visual);
    }
    return existing.value();
  }
  CHECK(!indices.empty());
  tab_groups::TabGroupId group = model->AddToNewGroup(indices);
  model->ChangeTabGroupVisuals(group, visual);
  // Do NOT sync->RemoveGroup(group) here. tab-group-sync auto-saves every
  // AddToNewGroup, but removing the saved copy immediately leaves the upstream
  // LocalTabGroupListener with a dangling saved_guid_: a later AddToExistingGroup
  // (AssignTabsToGroup adding a tab to this managed group during organize / agent
  // churn) then hits a FATAL CHECK(service_->GetGroup(saved_guid_).has_value()) in
  // LocalTabGroupListener::AddTabFromLocal and ABORTS the browser (the recurring
  // "dies under load" crash). Orphan prevention is handled WITHOUT this: the saved
  // copy is deleted when the group EMPTIES (ClearGroupIfEmpty), and any
  // shutdown-time leftovers are swept by the one-time startup backlog purge.
  return group;
}

// Moves exactly the tabs at |indices| into |group| in a single batched pair of
// TabStripModel mutations, instead of one AddToExistingGroup/RemoveFromGroup per
// index. AddToExistingGroup physically MOVES each added tab adjacent to the
// group, which renumbers the other indices — so calling it one index at a time
// (the old code) groups the wrong tabs after the first move, and is O(n^2).
//
// Formulation:
//   1. Scan |indices| ONCE, splitting into tabs not in any group (no-op moves,
//      just need adding) and tabs in a DIFFERENT group (must be removed first).
//      Capture the WebContents* for every tab that should end up in |group| but
//      isn't yet — pointers are stable across the moves that follow, indices are
//      not.
//   2. RemoveFromGroup() the wrong-group tabs in ONE call (this also moves
//      tabs, invalidating every captured index).
//   3. Re-resolve the captured WebContents* back to their CURRENT indices via
//      GetIndexOfWebContents (post-remove), sort ascending (AddToExistingGroup
//      requires a sorted vector), and AddToExistingGroup() them in ONE call.
// All mutation stays inside the deferred Reconcile(); no synchronous model
// mutation is introduced in an observer callback.
void AssignTabsToGroup(TabStripModel* model,
                       const std::vector<int>& indices,
                       tab_groups::TabGroupId group) {
  std::vector<int> need_move_out;
  // WebContents of every tab that should be in |group| but currently isn't
  // (either ungrouped or in a different group). Pointers survive the moves
  // below; indices do not.
  std::vector<content::WebContents*> targets;
  for (int index : indices) {
    std::optional<tab_groups::TabGroupId> cur = model->GetTabGroupForTab(index);
    if (!cur.has_value()) {
      targets.push_back(model->GetWebContentsAt(index));
    } else if (cur.value() != group) {
      need_move_out.push_back(index);
      targets.push_back(model->GetWebContentsAt(index));
    }
    // cur == group: already in the right group, leave it (no move, no churn).
  }

  // Step 2: pull the wrong-group tabs out in one batch. RemoveFromGroup requires
  // a sorted vector and moves tabs, so |targets| indices are now stale.
  if (!need_move_out.empty()) {
    std::sort(need_move_out.begin(), need_move_out.end());
    model->RemoveFromGroup(need_move_out);
  }

  // Step 3: re-resolve the target WebContents* to their post-remove indices,
  // sort, and add them all in one call.
  if (targets.empty()) {
    return;
  }
  std::vector<int> add_indices;
  add_indices.reserve(targets.size());
  for (content::WebContents* wc : targets) {
    int idx = model->GetIndexOfWebContents(wc);
    if (idx != TabStripModel::kNoTab) {
      add_indices.push_back(idx);
    }
  }
  if (!add_indices.empty()) {
    std::sort(add_indices.begin(), add_indices.end());
    model->AddToExistingGroup(add_indices, group);
  }
}

void ClearGroupIfEmpty(TabStripModel* model,
                       tab_groups::TabGroupSyncService* sync,
                       std::u16string_view prefix) {
  std::optional<tab_groups::TabGroupId> group = FindGroupWithPrefix(model, prefix);
  if (!group.has_value()) {
    return;
  }
  std::vector<int> grouped = IndicesInGroup(model, group.value());
  if (!grouped.empty()) {
    model->RemoveFromGroup(grouped);
  }
  // Delete any saved copy so the now-empty group can't become a locally-closed
  // orphan (RemoveFromGroup alone only marks it closed in the sync model).
  if (sync) {
    sync->RemoveGroup(group.value());
  }
}

// Removes every "Agent: *" native group that no longer has a live owner — i.e.
// whose title isn't claimed by any prefix in |live_prefixes|. Used to retire an
// agent's group once its last tab is gone (or all of them when no agent tabs
// remain). Snapshots the group list into |groups| FIRST because RemoveFromGroup
// (auto-deletes the now-empty group) and RemoveGroup both mutate the live list
// mid-iteration. Removal is crash-safe: an emptied group has a nullopt
// local_group_id (no live LocalTabGroupListener), so deleting its saved copy
// can't dangle a saved_guid_ — that was the e7b0252 abort, which required
// removing a still-LIVE group.
void ClearStaleAgentGroups(TabStripModel* model,
                           tab_groups::TabGroupSyncService* sync,
                           const std::set<std::u16string>& live_prefixes) {
  if (!model->group_model()) {
    return;
  }
  const std::vector<tab_groups::TabGroupId> groups =
      model->group_model()->ListTabGroups();
  for (const tab_groups::TabGroupId& id : groups) {
    TabGroup* group = model->group_model()->GetTabGroup(id);
    if (!group || !group->visual_data()) {
      continue;
    }
    const std::u16string& title = group->visual_data()->title();
    if (!base::StartsWith(title, std::u16string_view(kAgentGroupTitlePrefix))) {
      continue;
    }
    bool live = false;
    for (const std::u16string& p : live_prefixes) {
      if (base::StartsWith(title, p + u" (")) {
        live = true;
        break;
      }
    }
    if (live) {
      continue;
    }
    std::vector<int> indices = IndicesInGroup(model, id);
    if (!indices.empty()) {
      model->RemoveFromGroup(indices);
    }
    // Delete the saved copy so the retired group can't linger as a closed
    // orphan chip (RemoveFromGroup alone only marks it closed in the sync
    // model). Safe per the function comment (nullopt local_group_id).
    if (sync) {
      sync->RemoveGroup(id);
    }
  }
}
}  // namespace

AgentTabGrouper::AgentTabGrouper(TabStripModel* model) : model_(model) {
  model_->AddObserver(this);
  // Live-reload: re-open/close bookmark tabs when the user edits the list in
  // Settings (the gateway persists + posts NotifyBookmarkConfigChanged to the
  // UI thread).
  bookmark_config_subscription_ =
      grok_companion::AddBookmarkConfigChangedCallback(base::BindRepeating(
          &AgentTabGrouper::OnBookmarkConfigChanged,
          weak_factory_.GetWeakPtr()));
  Reconcile();
  MaybeScheduleSeed();
}

AgentTabGrouper::~AgentTabGrouper() {
  if (model_) {
    model_->RemoveObserver(this);
  }
}

void AgentTabGrouper::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // User priority: a real user tab gesture (click / keyboard select) revokes
  // any agent focus grant — the user always wins, even mid agent task. A direct
  // user tab click never reaches the gateway, so this observer is the only place
  // it can be detected. The agent's own granted /activate carries
  // CHANGE_REASON_NONE and does NOT reset the grant.
  if (selection.reason & TabStripModelObserver::CHANGE_REASON_USER_GESTURE) {
    agent_gateway::FocusArbiter::Get()->ResetToUser();
  }

  // A manually-closed bookmark tab must stick: drop its id from the persisted
  // config so a later ApplyBookmarkConfig() or a new window's seeder doesn't
  // re-open it. Only treat a real delete (kDeleted) as a close — a tab moved to
  // another window (kInsertedIntoOtherTabStrip) keeps its bookmark id. Skip
  // entirely while the strip is closing_all() (window close / shutdown): those
  // removals are kDeleted too, but must NOT wipe the persisted bookmark config —
  // those tabs are meant to re-open next launch. Never mutate config here
  // (re-entrancy); stage + defer to FlushClosedBookmarkConfigs.
  if (change.type() == TabStripModelChange::kRemoved &&
      !tab_strip_model->closing_all()) {
    if (const TabStripModelChange::Remove* removed = change.GetRemove()) {
      for (const TabStripModelChange::RemovedTab& r : removed->contents) {
        if (r.remove_reason != TabRemovedReason::kDeleted || !r.contents) {
          continue;
        }
        agent_gateway::TabOwnership* own =
            agent_gateway::TabOwnership::Get(r.contents);
        if (own && own->bookmark_node_id != 0) {
          closed_bookmark_ids_.push_back(own->bookmark_node_id);
        }
      }
      if (!closed_bookmark_ids_.empty() && !bookmark_remove_scheduled_) {
        bookmark_remove_scheduled_ = true;
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(&AgentTabGrouper::FlushClosedBookmarkConfigs,
                           weak_factory_.GetWeakPtr()));
      }
    }
  }

  ScheduleReconcile();
  MaybeScheduleSeed();
}

void AgentTabGrouper::FlushClosedBookmarkConfigs() {
  bookmark_remove_scheduled_ = false;
  std::vector<int64_t> ids;
  ids.swap(closed_bookmark_ids_);
  for (int64_t id : ids) {
    grok_companion::RemoveBookmarkConfig(id);
  }
}

void AgentTabGrouper::ScheduleReconcile() {
  // Defer Reconcile() out of the observer callback: it mutates the
  // TabStripModel (AddToNewGroup / AddToExistingGroup / ChangeTabGroupVisuals),
  // which re-enters the model and trips its re-entrancy guard if done
  // synchronously during a notification (e.g. while a tab is being activated).
  // Coalesce rapid changes into a single deferred pass.
  if (reconcile_scheduled_ || !model_) {
    return;
  }
  reconcile_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AgentTabGrouper::Reconcile, weak_factory_.GetWeakPtr()));
}

void AgentTabGrouper::Reconcile() {
  reconcile_scheduled_ = false;
  if (reconciling_ || !model_) {
    return;
  }
  reconciling_ = true;

  Browser* browser = nullptr;
  if (model_->count() > 0) {
    if (BrowserWindowInterface* bwi =
            GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
                model_->GetWebContentsAt(0))) {
      browser = bwi->GetBrowserForMigrationOnly();
    }
  }

  tab_groups::TabGroupSyncService* sync = SyncServiceFor(browser);

  // Continuous closed-orphan purge: delete EVERY saved group with no live
  // local_group_id(). In Xplorer all tab groups are ephemeral/auto-managed — the
  // three managed groups (Agent/Scheduled/Bookmarks) plus the one-shot "organize
  // tabs" topic groups — so a saved group with no live local_group_id() is just a
  // locally-closed orphan. They accumulate mid-session (each launch recreates the
  // managed groups since TabOwnership is runtime-only; each organize adds more),
  // cluttering the "Create New Tab Group" dropdown with dozens of stale entries.
  // Runs every Reconcile (not once) so orphans are cleaned promptly. Crash-safe:
  // a group with nullopt local_group_id has NO active LocalTabGroupListener (the
  // listener is created with the local id and torn down atomically when it goes
  // nullopt), so this can't dangle a saved_guid_ — that was the e7b0252 abort,
  // which required removing a still-LIVE group. GetAllGroups() is a by-value
  // snapshot, so removing while iterating is safe.
  //
  // BUT skip while a nested run loop is active (a context menu / tab drag): the
  // "Add to group" submenu lists closed orphans and captures their saved_guid at
  // build time; removing one mid-display would FATAL when the user clicks the
  // now-stale item (upstream CHECK in ExistingTabGroupSubMenuModel). The next
  // non-nested Reconcile cleans them.
  // NOTE: this purges ALL nullopt-local saved groups. Safe here because the fork
  // wires no account sign-in/sync — every closed group is a local orphan. If
  // account-based tab-group sync is ever enabled, gate this so it doesn't delete
  // remotely-saved closed groups across devices.
  if (sync && !base::RunLoop::IsNestedOnCurrentThread()) {
    for (const tab_groups::SavedTabGroup& g : sync->GetAllGroups()) {
      if (!g.local_group_id().has_value()) {
        sync->RemoveGroup(g.saved_guid());
      }
    }
  }

  std::vector<int> scheduled_indices;
  // Ad-hoc agent tabs bucketed by their owning agent: each distinct owner gets
  // its own native "Agent: <owner> (N)" group (agents own the tabs they open).
  std::map<std::string, std::vector<int>> adhoc_by_owner;
  std::vector<int> bookmark_indices;
  for (int i = 0; i < model_->count(); ++i) {
    content::WebContents* wc = model_->GetWebContentsAt(i);
    agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
    // Drop stale task_id stamps that leaked onto ad-hoc agent tabs.
    if (own && !own->task_id.empty() &&
        !agent_gateway::IsScheduledTaskTab(own)) {
      own->task_id.clear();
      if (browser) {
        tabs::TabInterface* tab = model_->GetTabAtIndex(i);
        if (tab && own->bookmark_node_id == 0) {
          SetTabRowVisible(browser, tab, true);
        }
      }
    }
    if (IsScheduledTaskWebContents(wc)) {
      scheduled_indices.push_back(i);
    } else if (IsAdHocAgentTab(wc)) {
      // IsAdHocAgentTab guarantees |own| is non-null with a non-empty owner.
      adhoc_by_owner[own->owner].push_back(i);
    } else if (own && own->bookmark_node_id != 0) {
      bookmark_indices.push_back(i);
    }
  }

  if (scheduled_indices.empty()) {
    ClearGroupIfEmpty(model_, sync, kScheduledGroupPrefix);
  } else {
    tab_groups::TabGroupId scheduled_group = EnsureGroup(
        model_, sync, kScheduledGroupPrefix, scheduled_indices,
        tab_groups::TabGroupColorId::kBlue);
    // Batched membership fix (see AssignTabsToGroup). EnsureGroup already set the
    // title/color, so no second ChangeTabGroupVisuals here.
    AssignTabsToGroup(model_, scheduled_indices, scheduled_group);
  }

  // One native "Agent: <owner> (N)" group per distinct agent owner. Each agent
  // owns the tabs it opens, so they no longer collapse into a single shared
  // group. Collect the live prefixes so ClearStaleAgentGroups can retire the
  // groups of agents that no longer have any open tabs (incl. all of them when
  // adhoc_by_owner is empty).
  std::set<std::u16string> live_agent_prefixes;
  for (const auto& [owner, indices] : adhoc_by_owner) {
    std::u16string prefix = kAgentGroupTitlePrefix + base::UTF8ToUTF16(owner);
    live_agent_prefixes.insert(prefix);
    // Stable per-owner color: hash the owner string so an agent keeps its color
    // regardless of how many other agents exist or what order they're in.
    tab_groups::TabGroupColorId color =
        kAgentColorCycle[base::PersistentHash(owner) %
                         std::size(kAgentColorCycle)];
    tab_groups::TabGroupId agent_group =
        EnsureGroup(model_, sync, prefix, indices, color);
    // Batched membership fix (see AssignTabsToGroup). EnsureGroup already set the
    // title/color, so no second ChangeTabGroupVisuals here.
    AssignTabsToGroup(model_, indices, agent_group);
  }
  ClearStaleAgentGroups(model_, sync, live_agent_prefixes);

  if (bookmark_indices.empty()) {
    ClearGroupIfEmpty(model_, sync, kBookmarksGroupPrefix);
  } else {
    tab_groups::TabGroupId bookmark_group = EnsureGroup(
        model_, sync, kBookmarksGroupPrefix, bookmark_indices,
        tab_groups::TabGroupColorId::kYellow);
    // Batched membership fix (see AssignTabsToGroup). EnsureGroup already set the
    // title/color, so no second ChangeTabGroupVisuals here.
    AssignTabsToGroup(model_, bookmark_indices, bookmark_group);
  }

  reconciling_ = false;
}

void AgentTabGrouper::MaybeScheduleSeed() {
  if (seeded_ || seed_scheduled_ || !model_ || model_->count() == 0) {
    return;
  }
  seed_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AgentTabGrouper::SeedDefaultBookmarks,
                                weak_factory_.GetWeakPtr()));
}

void AgentTabGrouper::SeedDefaultBookmarks() {
  seed_scheduled_ = false;
  if (seeded_ || !model_) {
    return;
  }

  Browser* browser = nullptr;
  if (model_->count() > 0) {
    if (BrowserWindowInterface* bwi =
            GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
                model_->GetWebContentsAt(0))) {
      browser = bwi->GetBrowserForMigrationOnly();
    }
  }
  if (!browser) {
    // No browser yet; a later OnTabStripModelChanged re-schedules the seed.
    return;
  }
  seeded_ = true;

  // The bookmark list is config-driven: read the user-editable
  // settings["bookmarks"] list. On first run it is empty, so materialize the
  // built-in kDefaultBookmarks[] into the config and persist it (so the user has
  // something to edit), then open from that same config.
  std::vector<base::DictValue> configs = grok_companion::GetBookmarkConfigs();
  if (configs.empty()) {
    int64_t synthetic_id = 0;
    for (const auto& bookmark : kDefaultBookmarks) {
      ++synthetic_id;
      const GURL url = ResolveBookmarkHref(bookmark.href);
      if (!url.is_valid()) {
        continue;
      }
      base::DictValue entry;
      entry.Set("id", base::NumberToString(synthetic_id));
      entry.Set("label", bookmark.label);
      entry.Set("url", url.spec());
      configs.push_back(std::move(entry));
    }
    grok_companion::SetBookmarkConfigs(configs);
  }

  bool opened_any = OpenMissingBookmarkTabs(browser, configs);

  // Let the deferred Reconcile form the native "Bookmarks" group; never group
  // synchronously here (TabStripModel re-entrancy).
  if (opened_any) {
    ScheduleReconcile();
  }
}

bool AgentTabGrouper::OpenMissingBookmarkTabs(
    Browser* browser,
    const std::vector<base::DictValue>& configs) {
  if (!browser || !model_) {
    return false;
  }
  // Collect synthetic ids already present so we don't re-open on a second
  // grouper for the same model (and to stay idempotent across launches once
  // tabs are restored with the tag).
  std::vector<int64_t> present_ids;
  for (int i = 0; i < model_->count(); ++i) {
    agent_gateway::TabOwnership* own =
        agent_gateway::TabOwnership::Get(model_->GetWebContentsAt(i));
    if (own && own->bookmark_node_id != 0) {
      present_ids.push_back(own->bookmark_node_id);
    }
  }

  bool opened_any = false;
  for (const base::DictValue& config : configs) {
    const std::string* id = config.FindString("id");
    const std::string* url_str = config.FindString("url");
    if (!id || !url_str) {
      continue;
    }
    int64_t node_id = 0;
    if (!base::StringToInt64(*id, &node_id) || node_id == 0) {
      continue;
    }
    if (std::find(present_ids.begin(), present_ids.end(), node_id) !=
        present_ids.end()) {
      continue;
    }
    const GURL url(*url_str);
    if (!url.is_valid()) {
      continue;
    }
    // Open in the background so the user's active tab keeps focus.
    NavigateParams params(browser, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    Navigate(&params);
    if (params.navigated_or_inserted_contents) {
      agent_gateway::TabOwnership::GetOrCreate(
          params.navigated_or_inserted_contents)
          ->bookmark_node_id = node_id;
      present_ids.push_back(node_id);
      opened_any = true;
    }
  }
  return opened_any;
}

void AgentTabGrouper::OnBookmarkConfigChanged() {
  // The notify fires synchronously (often from a UI-thread PostTask off the
  // gateway). Never mutate the TabStripModel here — defer to a coalesced task,
  // exactly like ScheduleReconcile(), so open/close run outside any model
  // notification/guard.
  if (bookmark_reload_scheduled_ || !model_) {
    return;
  }
  bookmark_reload_scheduled_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AgentTabGrouper::ApplyBookmarkConfig,
                                weak_factory_.GetWeakPtr()));
}

void AgentTabGrouper::ApplyBookmarkConfig() {
  bookmark_reload_scheduled_ = false;
  if (!model_) {
    return;
  }

  Browser* browser = nullptr;
  if (model_->count() > 0) {
    if (BrowserWindowInterface* bwi =
            GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
                model_->GetWebContentsAt(0))) {
      browser = bwi->GetBrowserForMigrationOnly();
    }
  }
  if (!browser) {
    return;
  }

  std::vector<base::DictValue> configs = grok_companion::GetBookmarkConfigs();

  // Set of bookmark_node_ids the config still wants open.
  std::vector<int64_t> wanted_ids;
  for (const base::DictValue& config : configs) {
    const std::string* id = config.FindString("id");
    int64_t node_id = 0;
    if (id && base::StringToInt64(*id, &node_id) && node_id != 0) {
      wanted_ids.push_back(node_id);
    }
  }

  // Close any open bookmark tab whose id is no longer in the config. Walk from
  // the end so closing one index doesn't shift the indices we still inspect.
  for (int i = model_->count() - 1; i >= 0; --i) {
    content::WebContents* wc = model_->GetWebContentsAt(i);
    agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
    if (!own || own->bookmark_node_id == 0) {
      continue;
    }
    if (std::find(wanted_ids.begin(), wanted_ids.end(),
                  own->bookmark_node_id) == wanted_ids.end()) {
      model_->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
    }
  }

  // If the close loop emptied the Bookmarks group, delete its saved copy too so
  // it can't linger as a closed orphan chip (RemoveFromGroup alone only marks it
  // locally-closed in the sync model).
  ClearGroupIfEmpty(model_, SyncServiceFor(browser), kBookmarksGroupPrefix);

  // Open background tabs for config ids not yet present (stamps
  // bookmark_node_id). Reuses the present-id de-dupe.
  OpenMissingBookmarkTabs(browser, configs);

  // Let the deferred Reconcile re-title / regroup the "Bookmarks" group.
  ScheduleReconcile();
}

}  // namespace xplorer