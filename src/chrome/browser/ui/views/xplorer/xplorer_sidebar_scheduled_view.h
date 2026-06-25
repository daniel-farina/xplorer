// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_SCHEDULED_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_SCHEDULED_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace xplorer {

// Read-only "Scheduled" section in the Arc-style sidebar, rendered BELOW the tab
// list (sidebar order: Bookmarks -> Tabs -> Scheduled). Mirrors
// XplorerSidebarBookmarksView: a section label + a column of row buttons, one
// per background-task scheduler job.
//
// The job list lives on the gateway IO thread (agent_gateway::Scheduler), so
// this view never reads it directly; it pulls a thread-safe snapshot via
// Scheduler::GetJobsAsync() on construction, on a light refresh timer, and when
// shown, then rebuilds its rows from the returned dict. v1 is read-only: rows do
// not wire focus/activation.
class XplorerSidebarScheduledView : public views::View {
  METADATA_HEADER(XplorerSidebarScheduledView, views::View)

 public:
  XplorerSidebarScheduledView();
  XplorerSidebarScheduledView(const XplorerSidebarScheduledView&) = delete;
  XplorerSidebarScheduledView& operator=(const XplorerSidebarScheduledView&) =
      delete;
  ~XplorerSidebarScheduledView() override;

  // views::View:
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

 private:
  // Kick off an async snapshot fetch; the reply lands in OnJobs() on this
  // (UI) sequence.
  void Refresh();
  // Rebuild the section's children from a {"version":1,"jobs":[...]} snapshot.
  void OnJobs(base::DictValue snapshot);

  base::RepeatingTimer refresh_timer_;
  base::WeakPtrFactory<XplorerSidebarScheduledView> weak_factory_{this};
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_SCHEDULED_VIEW_H_
