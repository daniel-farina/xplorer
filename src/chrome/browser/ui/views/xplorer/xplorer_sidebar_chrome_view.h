// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_CHROME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_CHROME_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class Profile;

namespace views {
class View;
}  // namespace views

namespace xplorer {

class XplorerSidebarBookmarksView;
class XplorerToolbarView;

enum class ToolbarPlacement;

// Arc-style chrome injected at the top of the vertical tab strip: bookmarks,
// optional Grok pill toolbar, and a "Tabs" section label.
class XplorerSidebarChromeView : public views::View,
                                 public views::ContextMenuController {
  METADATA_HEADER(XplorerSidebarChromeView, views::View)

 public:
  XplorerSidebarChromeView(BrowserWindowInterface* browser, Profile* profile);
  XplorerSidebarChromeView(const XplorerSidebarChromeView&) = delete;
  XplorerSidebarChromeView& operator=(const XplorerSidebarChromeView&) =
      delete;
  ~XplorerSidebarChromeView() override;

  // Hosts the shared XplorerToolbarView when toolbar.placement == sidebar.
  views::View* toolbar_host() { return toolbar_host_; }
  void AttachToolbar(XplorerToolbarView* toolbar);

  // Shows/hides the bookmarks + toolbar block; only "Tabs" remains when at top.
  void UpdateForToolbarPlacement(ToolbarPlacement placement);

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<Profile> profile_;
  raw_ptr<XplorerSidebarBookmarksView> bookmarks_ = nullptr;
  raw_ptr<views::View> toolbar_host_ = nullptr;
  raw_ptr<views::View> bookmarks_separator_ = nullptr;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_CHROME_VIEW_H_