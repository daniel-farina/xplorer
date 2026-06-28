// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SETTINGS_NAV_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SETTINGS_NAV_H_

#include <string_view>

class BrowserWindowInterface;

namespace xplorer {

// Opens the companion settings page (http://127.0.0.1:9334/settings). If |pane|
// is non-empty, deep-links to that pane via a URL fragment that settings.js
// reads on load (e.g. pane="bookmarks" -> /settings#bookmarks). When
// |in_new_tab| is true, opens in a new foreground tab instead of navigating
// the active tab.
void OpenXplorerSettings(BrowserWindowInterface* browser,
                         std::string_view pane = {},
                         bool in_new_tab = false);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SETTINGS_NAV_H_