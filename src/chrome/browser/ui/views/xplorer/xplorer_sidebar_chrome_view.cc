// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"

#include <memory>

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_bookmarks_view.h"
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
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  bookmarks_ = AddChildView(
      std::make_unique<XplorerSidebarBookmarksView>(browser_, profile_));

  auto toolbar_host = std::make_unique<views::View>();
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

void XplorerSidebarChromeView::AttachToolbar(XplorerToolbarView* toolbar) {
  if (!toolbar_host_ || !toolbar) {
    return;
  }
  toolbar_host_->RemoveAllChildViews();
  toolbar->SetVerticalLayout(true);
  if (toolbar->parent() != toolbar_host_) {
    if (toolbar->parent()) {
      toolbar->parent()->RemoveChildView(toolbar);
    }
    toolbar_host_->AddChildView(toolbar);
  }
  toolbar->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                               views::MaximumFlexSizeRule::kPreferred)
          .WithWeight(0));
  UpdateChromeState();
}

void XplorerSidebarChromeView::UpdateChromeState() {
  const bool show_bookmarks_block =
      GetToolbarPlacement() == ToolbarPlacement::kSidebar && GetToolbarVisible();
  if (bookmarks_) {
    bookmarks_->SetVisible(show_bookmarks_block);
  }
  if (toolbar_host_) {
    toolbar_host_->SetVisible(show_bookmarks_block);
  }
  if (bookmarks_separator_) {
    bookmarks_separator_->SetVisible(show_bookmarks_block);
  }
  PreferredSizeChanged();
  InvalidateLayout();
}

BEGIN_METADATA(XplorerSidebarChromeView)
END_METADATA

}  // namespace xplorer