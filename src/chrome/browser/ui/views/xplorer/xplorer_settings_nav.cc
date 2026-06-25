// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_settings_nav.h"

#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"

namespace xplorer {

void OpenXplorerSettings(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  const GURL url = grok_companion::GetCompanionURL().Resolve("/settings");
  if (!url.is_valid()) {
    return;
  }
  tabs::TabInterface* tab = browser->GetActiveTabInterface();
  if (!tab) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  if (!contents) {
    return;
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  contents->GetController().LoadURLWithParams(params);
}

}  // namespace xplorer