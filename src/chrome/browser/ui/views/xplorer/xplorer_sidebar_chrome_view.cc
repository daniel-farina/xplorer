// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"

#include <memory>

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_section_label.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace xplorer {

namespace {
constexpr gfx::Insets kSectionLabelMargins = gfx::Insets::TLBR(4, 0, 0, 0);
}  // namespace

XplorerSidebarChromeView::XplorerSidebarChromeView(
    BrowserWindowInterface* browser,
    Profile* profile)
    : browser_(browser), profile_(profile) {
  SetBackground(nullptr);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Bookmarks are now a native "Bookmarks" tab group (seeded by AgentTabGrouper);
  // the old custom sidebar bookmark list and the Grok pill toolbar are gone. All
  // that remains is the "Tabs" section label above the vertical tab list.
  auto* tabs_label = AddChildView(
      std::make_unique<XplorerSidebarSectionLabel>(u"Tabs"));
  tabs_label->SetProperty(views::kMarginsKey, kSectionLabelMargins);
}

XplorerSidebarChromeView::~XplorerSidebarChromeView() = default;

BEGIN_METADATA(XplorerSidebarChromeView)
END_METADATA

}  // namespace xplorer
