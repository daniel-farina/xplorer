// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_settings_nav.h"

#include <string>
#include <string_view>

#include "chrome/browser/grok_companion/grok_companion_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace xplorer {

void OpenXplorerSettings(BrowserWindowInterface* browser,
                         std::string_view pane,
                         bool in_new_tab) {
  if (!browser) {
    return;
  }
  // Deep-link to a specific pane via a URL fragment (settings.js reads it on
  // load). "/settings" alone opens the default (General) pane.
  std::string path = "/settings";
  if (!pane.empty()) {
    path += "#";
    path += pane;
  }
  const GURL url = grok_companion::GetCompanionURL().Resolve(path);
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
  if (in_new_tab) {
    // Open settings in a new foreground tab (don't clobber the active tab).
    // OpenURL on the active contents routes through its delegate (the browser
    // window), so the new tab lands in this same window.
    content::OpenURLParams open_params(
        url, content::Referrer(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui::PAGE_TRANSITION_AUTO_BOOKMARK, /*is_renderer_initiated=*/false);
    contents->OpenURL(open_params, /*navigation_handle_callback=*/{});
    return;
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  contents->GetController().LoadURLWithParams(params);
}

}  // namespace xplorer