// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TABS_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TABS_H_

#include "url/gurl.h"

class Browser;
class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace xplorer {

// Shows or hides a tab's row in the vertical tab strip (Arc-style bookmark tabs
// live in the sidebar and are hidden from the Tabs list while pinned to a URL).
void SetTabRowVisible(Browser* browser, tabs::TabInterface* tab, bool visible);

// Opens |url| as a sidebar bookmark tab: reuses an existing tab tagged with the
// same URL, otherwise opens a new tab, tags it, and hides its row from Tabs.
void OpenBookmarkTab(BrowserWindowInterface* browser, const GURL& url);

// True when |wc| is tagged as a sidebar bookmark tab still on |bookmark_url|'s
// host (used for row highlighting and detach detection).
bool IsBookmarkTabOnUrl(content::WebContents* wc, const GURL& bookmark_url);

// Re-applies hidden rows for all bookmark-tagged tabs (call after tab-strip
// layout churn that may have re-shown rows).
void ReassertHiddenBookmarkTabRows(Browser* browser);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TABS_H_