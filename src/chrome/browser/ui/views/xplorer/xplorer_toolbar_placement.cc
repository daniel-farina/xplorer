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

  const ToolbarPlacement placement = GetToolbarPlacement();
  if (placement == ToolbarPlacement::kSidebar && sidebar_chrome) {
    sidebar_chrome->AttachToolbar(toolbar);
    sidebar_chrome->UpdateForToolbarPlacement(ToolbarPlacement::kSidebar);
    toolbar->SetVisible(true);
  } else {
    toolbar->SetVerticalLayout(false);
    views::View* top = browser_view->top_container();
    if (top && toolbar->parent() != top) {
      if (toolbar->parent()) {
        toolbar->parent()->RemoveChildView(toolbar);
      }
      top->AddChildView(toolbar);
    }
    toolbar->SetVisible(true);
    if (sidebar_chrome) {
      sidebar_chrome->UpdateForToolbarPlacement(ToolbarPlacement::kTop);
    }
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

}  // namespace xplorer