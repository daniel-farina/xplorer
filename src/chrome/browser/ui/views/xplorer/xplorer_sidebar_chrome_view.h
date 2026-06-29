// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_CHROME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_CHROME_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class Profile;

namespace xplorer {

// Arc-style chrome injected at the top of the vertical tab strip. Bookmarks are
// now a native "Bookmarks" tab group, so this is just the "Tabs" section label
// above the vertical tab list.
class XplorerSidebarChromeView : public views::View {
  METADATA_HEADER(XplorerSidebarChromeView, views::View)

 public:
  XplorerSidebarChromeView(BrowserWindowInterface* browser, Profile* profile);
  XplorerSidebarChromeView(const XplorerSidebarChromeView&) = delete;
  XplorerSidebarChromeView& operator=(const XplorerSidebarChromeView&) =
      delete;
  ~XplorerSidebarChromeView() override;

 private:
  const raw_ptr<BrowserWindowInterface> browser_;
  [[maybe_unused]] const raw_ptr<Profile> profile_;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_CHROME_VIEW_H_
