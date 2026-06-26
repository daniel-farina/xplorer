// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_agent_tab_grouper.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/agent_gateway/focus_arbiter.h"
#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/xplorer/xplorer_scheduled_task_tabs.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace xplorer {

namespace {
constexpr char16_t kAgentGroupPrefix[] = u"Agent tabs";
constexpr char16_t kScheduledGroupPrefix[] = u"Scheduled task tabs";
constexpr char16_t kBookmarksGroupPrefix[] = u"Bookmarks";

// Hardcoded xAI default bookmarks, seeded as always-open tabs in the native
// "Bookmarks" group at launch. Transcribed (label, href) IN ORDER from
// xplorer_toolbar_view.cc kDefaultPills — keep the two in sync. Gateway-relative
// hrefs ("/apps", "/switch-home?...") resolve against the companion base; absolute
// https hrefs are used as-is. The synthetic id stamped on each tab is its array
// index + 1.
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

std::u16string GroupTitle(const char16_t* prefix, int count) {
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
    const char16_t* prefix) {
  if (!model->group_model()) {
    return std::nullopt;
  }
  for (const tab_groups::TabGroupId& id :
       model->group_model()->ListTabGroups()) {
    TabGroup* group = model->group_model()->GetTabGroup(id);
    if (group && group->visual_data() &&
        base::StartsWith(group->visual_data()->title(), prefix)) {
      return id;
    }
  }
  return std::nullopt;
}

tab_groups::TabGroupId EnsureGroup(TabStripModel* model,
                                   const char16_t* prefix,
                                   const std::vector<int>& indices,
                                   tab_groups::TabGroupColorId color) {
  std::optional<tab_groups::TabGroupId> existing = FindGroupWithPrefix(model, prefix);
  tab_groups::TabGroupVisualData visual(
      GroupTitle(prefix, static_cast<int>(indices.size())), color);
  if (existing.has_value()) {
    model->ChangeTabGroupVisuals(existing.value(), visual);
    return existing.value();
  }
  CHECK(!indices.empty());
  tab_groups::TabGroupId group = model->AddToNewGroup(indices);
  model->ChangeTabGroupVisuals(group, visual);
  return group;
}

void ClearGroupIfEmpty(TabStripModel* model, const char16_t* prefix) {
  std::optional<tab_groups::TabGroupId> group = FindGroupWithPrefix(model, prefix);
  if (!group.has_value()) {
    return;
  }
  std::vector<int> grouped = IndicesInGroup(model, group.value());
  if (!grouped.empty()) {
    model->RemoveFromGroup(grouped);
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
  ScheduleReconcile();
  MaybeScheduleSeed();
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
  if (browser) {
    ReassertHiddenScheduledTaskTabRows(browser);
  }

  std::vector<int> scheduled_indices;
  std::vector<int> adhoc_indices;
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
      adhoc_indices.push_back(i);
    } else if (own && own->bookmark_node_id != 0) {
      bookmark_indices.push_back(i);
    }
  }

  if (scheduled_indices.empty()) {
    ClearGroupIfEmpty(model_, kScheduledGroupPrefix);
  } else {
    tab_groups::TabGroupId scheduled_group = EnsureGroup(
        model_, kScheduledGroupPrefix, scheduled_indices,
        tab_groups::TabGroupColorId::kBlue);
    for (int index : scheduled_indices) {
      if (!model_->GetTabGroupForTab(index).has_value()) {
        model_->AddToExistingGroup({index}, scheduled_group);
      } else if (model_->GetTabGroupForTab(index).value() != scheduled_group) {
        model_->RemoveFromGroup({index});
        model_->AddToExistingGroup({index}, scheduled_group);
      }
    }
    tab_groups::TabGroupVisualData scheduled_visual(
        GroupTitle(kScheduledGroupPrefix,
                   static_cast<int>(scheduled_indices.size())),
        tab_groups::TabGroupColorId::kBlue);
    model_->ChangeTabGroupVisuals(scheduled_group, scheduled_visual);
  }

  if (adhoc_indices.empty()) {
    ClearGroupIfEmpty(model_, kAgentGroupPrefix);
  } else {
    tab_groups::TabGroupId agent_group = EnsureGroup(
        model_, kAgentGroupPrefix, adhoc_indices,
        tab_groups::TabGroupColorId::kGrey);
    for (int index : adhoc_indices) {
      if (!model_->GetTabGroupForTab(index).has_value()) {
        model_->AddToExistingGroup({index}, agent_group);
      } else if (model_->GetTabGroupForTab(index).value() != agent_group) {
        model_->RemoveFromGroup({index});
        model_->AddToExistingGroup({index}, agent_group);
      }
    }
    tab_groups::TabGroupVisualData agent_visual(
        GroupTitle(kAgentGroupPrefix, static_cast<int>(adhoc_indices.size())),
        tab_groups::TabGroupColorId::kGrey);
    model_->ChangeTabGroupVisuals(agent_group, agent_visual);
  }

  if (bookmark_indices.empty()) {
    ClearGroupIfEmpty(model_, kBookmarksGroupPrefix);
  } else {
    tab_groups::TabGroupId bookmark_group = EnsureGroup(
        model_, kBookmarksGroupPrefix, bookmark_indices,
        tab_groups::TabGroupColorId::kYellow);
    for (int index : bookmark_indices) {
      if (!model_->GetTabGroupForTab(index).has_value()) {
        model_->AddToExistingGroup({index}, bookmark_group);
      } else if (model_->GetTabGroupForTab(index).value() != bookmark_group) {
        model_->RemoveFromGroup({index});
        model_->AddToExistingGroup({index}, bookmark_group);
      }
    }
    tab_groups::TabGroupVisualData bookmark_visual(
        GroupTitle(kBookmarksGroupPrefix,
                   static_cast<int>(bookmark_indices.size())),
        tab_groups::TabGroupColorId::kYellow);
    model_->ChangeTabGroupVisuals(bookmark_group, bookmark_visual);
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

  // Open background tabs for config ids not yet present (stamps
  // bookmark_node_id). Reuses the present-id de-dupe.
  OpenMissingBookmarkTabs(browser, configs);

  // Let the deferred Reconcile re-title / regroup the "Bookmarks" group.
  ScheduleReconcile();
}

}  // namespace xplorer