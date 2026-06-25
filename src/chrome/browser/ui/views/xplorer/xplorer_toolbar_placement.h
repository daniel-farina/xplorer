// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_PLACEMENT_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_PLACEMENT_H_

class BrowserView;
class BrowserWindowInterface;

namespace xplorer {

class XplorerSidebarChromeView;

// Moves the shared XplorerToolbarView between top_container_ and the vertical
// tab strip sidebar chrome according to grok_settings.json toolbar.placement.
void ApplyToolbarPlacement(BrowserView* browser_view,
                           XplorerSidebarChromeView* sidebar_chrome);

// Convenience wrapper for context-menu actions.
void ApplyToolbarPlacementForBrowser(BrowserWindowInterface* browser);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_PLACEMENT_H_