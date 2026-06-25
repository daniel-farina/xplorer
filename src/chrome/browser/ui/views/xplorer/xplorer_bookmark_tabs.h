// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TABS_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TABS_H_

#include <cstdint>

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

void SetTabRowVisible(Browser* browser, tabs::TabInterface* tab, bool visible);

// Arc-style bookmark tabs: each bookmark is a dedicated WebContents, launched
// from the Bookmarks sidebar. Rows stay hidden in the Tabs strip; the sidebar
// row is the tab affordance.
void OpenBookmarkTab(BrowserWindowInterface* browser,
                     const GURL& url,
                     int64_t bookmark_node_id = 0);

bool IsBookmarkWebContents(content::WebContents* wc);
bool IsBookmarkTabOnUrl(content::WebContents* wc, const GURL& bookmark_url);
bool IsBookmarkTabForNode(content::WebContents* wc, int64_t bookmark_node_id);

// Vertical strip paints |last user tab| as selected while bookmark content shows.
bool ShouldShowAsActiveInStrip(tabs::TabInterface* tab);

void ReassertHiddenBookmarkTabRows(Browser* browser);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TABS_H_