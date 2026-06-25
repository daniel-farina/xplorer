// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_agent_tab_grouper.h"

#include <string>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/agent_gateway/focus_arbiter.h"
#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tabs.h"
#include "chrome/browser/ui/views/xplorer/xplorer_scheduled_task_tabs.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace xplorer {

namespace {
constexpr char16_t kAgentGroupPrefix[] = u"Agent tabs";
constexpr char16_t kScheduledGroupPrefix[] = u"Scheduled task tabs";

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
  Reconcile();
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
    ReassertHiddenBookmarkTabRows(browser);
  }

  std::vector<int> scheduled_indices;
  std::vector<int> adhoc_indices;
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

  reconciling_ = false;
}

}  // namespace xplorer