// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_scheduled_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/agent_gateway/scheduler.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_row_button.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_section_label.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace xplorer {

namespace {

// How often the section re-pulls the job snapshot. Light: the schedule list
// changes rarely, and a stale last_status hint for a few seconds is harmless.
constexpr base::TimeDelta kRefreshInterval = base::Seconds(10);

gfx::Insets SidebarRowMargins() {
  return gfx::Insets::TLBR(2, 0, 2, 0);
}

// A short status hint appended to a job's label, mirroring the design's
// last_status vocabulary (ok | failed | running | skipped | deferred). Returns
// an empty string for the quiet/normal states so idle rows stay clean.
std::u16string StatusHint(const std::string& last_status) {
  if (last_status == "running")
    return u"running…";
  if (last_status == "failed")
    return u"failed";
  if (last_status == "deferred")
    return u"deferred";
  return std::u16string();
}

}  // namespace

XplorerSidebarScheduledView::XplorerSidebarScheduledView() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Pull the initial snapshot; the section stays empty (collapsed) until it
  // arrives.
  Refresh();
  refresh_timer_.Start(
      FROM_HERE, kRefreshInterval,
      base::BindRepeating(&XplorerSidebarScheduledView::Refresh,
                          base::Unretained(this)));
}

XplorerSidebarScheduledView::~XplorerSidebarScheduledView() = default;

void XplorerSidebarScheduledView::VisibilityChanged(views::View* starting_from,
                                                    bool is_visible) {
  if (is_visible) {
    Refresh();
  }
}

void XplorerSidebarScheduledView::Refresh() {
  agent_gateway::Scheduler::Get()->GetJobsAsync(
      base::BindOnce(&XplorerSidebarScheduledView::OnJobs,
                     weak_factory_.GetWeakPtr()));
}

void XplorerSidebarScheduledView::OnJobs(base::DictValue snapshot) {
  RemoveAllChildViews();

  const base::ListValue* jobs = snapshot.FindList("jobs");
  if (!jobs || jobs->empty()) {
    // Empty state: render nothing (no header), matching how the bookmarks
    // section stays collapsed when there is nothing to show.
    PreferredSizeChanged();
    return;
  }

  auto* header = AddChildView(
      std::make_unique<XplorerSidebarSectionLabel>(u"Scheduled"));
  header->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));

  for (const base::Value& entry : *jobs) {
    if (!entry.is_dict())
      continue;
    const base::DictValue& job = entry.GetDict();

    const std::string* label = job.FindString("label");
    const std::string* id = job.FindString("id");
    const std::string* last_status = job.FindString("last_status");
    const bool enabled = job.FindBool("enabled").value_or(true);

    // Prefer the user-set label; fall back to the id so a label-less job is
    // still identifiable.
    std::u16string text;
    if (label && !label->empty()) {
      text = base::UTF8ToUTF16(*label);
    } else if (id && !id->empty()) {
      text = base::UTF8ToUTF16(*id);
    } else {
      text = u"(untitled task)";
    }

    const std::u16string hint =
        last_status ? StatusHint(*last_status) : std::u16string();
    if (!enabled) {
      text += u" (paused)";
    } else if (!hint.empty()) {
      text += u" — " + hint;
    }

    // v1 is read-only: clicking a row is a no-op (an empty PressedCallback). It
    // does not select, focus, or activate anything.
    auto button = std::make_unique<XplorerSidebarRowButton>(
        views::Button::PressedCallback(), text);
    button->SetProperty(views::kMarginsKey, SidebarRowMargins());
    if (!hint.empty()) {
      button->SetTooltipText(hint);
    }
    AddChildView(std::move(button));
  }

  PreferredSizeChanged();
}

BEGIN_METADATA(XplorerSidebarScheduledView)
END_METADATA

}  // namespace xplorer
