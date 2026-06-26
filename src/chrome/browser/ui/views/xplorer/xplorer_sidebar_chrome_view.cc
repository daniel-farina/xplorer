// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"

#include <memory>

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_section_label.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_prefs.h"
#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_view.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/geometry/point.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view_class_properties.h"

namespace xplorer {

namespace {
constexpr gfx::Insets kSectionLabelMargins = gfx::Insets::TLBR(4, 0, 0, 0);
}  // namespace

XplorerSidebarChromeView::XplorerSidebarChromeView(
    BrowserWindowInterface* browser,
    Profile* profile)
    : browser_(browser), profile_(profile) {
  set_context_menu_controller(this);
  SetBackground(nullptr);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Bookmarks are now a native "Bookmarks" tab group (seeded by AgentTabGrouper);
  // the old custom sidebar bookmark list is gone.
  auto toolbar_host = std::make_unique<views::View>();
  toolbar_host->SetBackground(nullptr);
  auto* host_layout = toolbar_host->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  host_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  toolbar_host_ = AddChildView(std::move(toolbar_host));

  bookmarks_separator_ = AddChildView(std::make_unique<views::Separator>());
  auto* tabs_label = AddChildView(
      std::make_unique<XplorerSidebarSectionLabel>(u"Tabs"));
  tabs_label->SetProperty(views::kMarginsKey, kSectionLabelMargins);

  UpdateChromeState();
}

XplorerSidebarChromeView::~XplorerSidebarChromeView() = default;

void XplorerSidebarChromeView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!browser_) {
    return;
  }
  Browser* b = browser_->GetBrowserForMigrationOnly();
  if (!b) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(b);
  XplorerToolbarView* toolbar =
      browser_view ? browser_view->xplorer_toolbar() : nullptr;
  if (toolbar) {
    toolbar->ShowContextMenuForViewImpl(source, point, source_type);
  }
}

void XplorerSidebarChromeView::UpdateChromeState() {
  const bool show_toolbar_in_sidebar =
      GetToolbarPlacement() == ToolbarPlacement::kSidebar && GetToolbarVisible();
  if (toolbar_host_) {
    toolbar_host_->SetVisible(show_toolbar_in_sidebar);
  }
  if (bookmarks_separator_) {
    bookmarks_separator_->SetVisible(show_toolbar_in_sidebar);
  }
  PreferredSizeChanged();
  InvalidateLayout();
}

BEGIN_METADATA(XplorerSidebarChromeView)
END_METADATA

}  // namespace xplorer