// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_placement.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_prefs.h"
#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_view.h"
#include "content/public/browser/browser_thread.h"
#include "ui/views/view_class_properties.h"

namespace xplorer {

void ApplyToolbarPlacement(BrowserView* browser_view,
                           XplorerSidebarChromeView* sidebar_chrome) {
  if (!browser_view) {
    return;
  }
  XplorerToolbarView* toolbar = browser_view->xplorer_toolbar();
  if (!toolbar) {
    return;
  }

  const bool visible = GetToolbarVisible();

  // XPLORER: the Grok-apps pill toolbar no longer appears in the sidebar rail
  // (bookmarks are a native tab group now). Keep only the top-container
  // placement so the row can't reappear in the sidebar.
  views::View* top = browser_view->top_container();
  if (top && toolbar->parent() != top) {
    if (toolbar->parent()) {
      toolbar->parent()->RemoveChildView(toolbar);
    }
    top->AddChildView(toolbar);
  }
  toolbar->ClearProperty(views::kFlexBehaviorKey);
  toolbar->SetVerticalLayout(false);

  toolbar->SetVisible(visible);

  if (sidebar_chrome) {
    sidebar_chrome->UpdateChromeState();
  }

  browser_view->InvalidateLayout();
}

void ApplyToolbarPlacementForBrowser(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  Browser* b = browser->GetBrowserForMigrationOnly();
  if (!b) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(b);
  if (!browser_view) {
    return;
  }
  ApplyToolbarPlacement(browser_view, browser_view->xplorer_sidebar_chrome());
}

void ScheduleApplyToolbarPlacementForBrowser(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&ApplyToolbarPlacementForBrowser, browser));
}

}  // namespace xplorer