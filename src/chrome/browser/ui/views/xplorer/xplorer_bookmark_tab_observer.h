// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TAB_OBSERVER_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TAB_OBSERVER_H_

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_observer.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

namespace xplorer {

// Watches a sidebar bookmark tab; when it navigates off the bookmark host the tab
// row is re-shown in the vertical Tabs strip and the bookmark tag is cleared.
class XplorerBookmarkTabObserver : public content::WebContentsObserver {
 public:
  XplorerBookmarkTabObserver(Browser* browser, content::WebContents* contents);
  XplorerBookmarkTabObserver(const XplorerBookmarkTabObserver&) = delete;
  XplorerBookmarkTabObserver& operator=(const XplorerBookmarkTabObserver&) =
      delete;
  ~XplorerBookmarkTabObserver() override;

  static void ObserveBookmarkTab(Browser* browser, content::WebContents* wc);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  const raw_ptr<Browser> browser_;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_BOOKMARK_TAB_OBSERVER_H_