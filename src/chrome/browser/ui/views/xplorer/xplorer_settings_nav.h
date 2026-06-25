// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SETTINGS_NAV_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SETTINGS_NAV_H_

class BrowserWindowInterface;

namespace xplorer {

// Opens the companion settings page (http://127.0.0.1:9334/settings).
void OpenXplorerSettings(BrowserWindowInterface* browser);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SETTINGS_NAV_H_