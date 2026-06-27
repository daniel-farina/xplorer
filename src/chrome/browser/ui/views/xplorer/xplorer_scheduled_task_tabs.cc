// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_scheduled_task_tabs.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"
#include "components/tabs/public/tab_interface.h"

namespace xplorer {

namespace {

VerticalTabStripView* GetVerticalTabStripView(Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->vertical_tab_strip_region_view()) {
    return nullptr;
  }
  return static_cast<VerticalTabStripView*>(
      browser_view->vertical_tab_strip_region_view()->GetTabStripView());
}

}  // namespace

void SetTabRowVisible(Browser* browser,
                      tabs::TabInterface* tab,
                      bool visible) {
  if (!browser || !tab) {
    return;
  }
  VerticalTabStripView* strip = GetVerticalTabStripView(browser);
  if (!strip) {
    return;
  }
  strip->SetTabRowVisible(tab->GetHandle(), visible);
}

}  // namespace xplorer