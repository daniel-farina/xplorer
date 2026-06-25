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
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"

namespace xplorer {

namespace {
constexpr char16_t kAgentGroupPrefix[] = u"Agent tabs";

bool IsAgentTab(content::WebContents* wc) {
  agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
  return own && !own->owner.empty();
}

std::u16string AgentGroupTitle(int count) {
  return std::u16string(kAgentGroupPrefix) + u" (" +
         base::NumberToString16(count) + u")";
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

std::optional<tab_groups::TabGroupId> AgentTabGrouper::FindAgentGroup() const {
  if (!model_->group_model()) {
    return std::nullopt;
  }
  for (const tab_groups::TabGroupId& id :
       model_->group_model()->ListTabGroups()) {
    TabGroup* group = model_->group_model()->GetTabGroup(id);
    if (group && group->visual_data() &&
        base::StartsWith(group->visual_data()->title(), kAgentGroupPrefix)) {
      return id;
    }
  }
  return std::nullopt;
}

tab_groups::TabGroupId AgentTabGrouper::EnsureAgentGroup(int count) {
  std::optional<tab_groups::TabGroupId> existing = FindAgentGroup();
  tab_groups::TabGroupVisualData visual(AgentGroupTitle(count),
                                        tab_groups::TabGroupColorId::kGrey);
  if (existing.has_value()) {
    model_->ChangeTabGroupVisuals(existing.value(), visual);
    return existing.value();
  }
  std::vector<int> indices;
  for (int i = 0; i < model_->count(); ++i) {
    if (IsAgentTab(model_->GetWebContentsAt(i))) {
      indices.push_back(i);
    }
  }
  CHECK(!indices.empty());
  tab_groups::TabGroupId group = model_->AddToNewGroup(indices);
  model_->ChangeTabGroupVisuals(group, visual);
  return group;
}

void AgentTabGrouper::Reconcile() {
  reconcile_scheduled_ = false;
  if (reconciling_ || !model_) {
    return;
  }
  reconciling_ = true;

  std::vector<int> agent_indices;
  for (int i = 0; i < model_->count(); ++i) {
    if (IsAgentTab(model_->GetWebContentsAt(i))) {
      agent_indices.push_back(i);
    }
  }

  std::optional<tab_groups::TabGroupId> agent_group = FindAgentGroup();

  if (agent_indices.empty()) {
    if (agent_group.has_value()) {
      std::vector<int> grouped = IndicesInGroup(model_, agent_group.value());
      if (!grouped.empty()) {
        model_->RemoveFromGroup(grouped);
      }
    }
    reconciling_ = false;
    return;
  }

  tab_groups::TabGroupId group = EnsureAgentGroup(
      static_cast<int>(agent_indices.size()));

  for (int index : agent_indices) {
    if (!model_->GetTabGroupForTab(index).has_value()) {
      model_->AddToExistingGroup({index}, group);
    } else if (model_->GetTabGroupForTab(index).value() != group) {
      model_->RemoveFromGroup({index});
      model_->AddToExistingGroup({index}, group);
    }
  }

  tab_groups::TabGroupVisualData visual(
      AgentGroupTitle(static_cast<int>(agent_indices.size())),
      tab_groups::TabGroupColorId::kGrey);
  model_->ChangeTabGroupVisuals(group, visual);

  reconciling_ = false;
}

}  // namespace xplorer