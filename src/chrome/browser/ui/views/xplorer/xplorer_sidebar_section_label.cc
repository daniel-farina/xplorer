// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_section_label.h"

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/label.h"

namespace xplorer {

namespace {
constexpr int kSectionHeight = 28;
}  // namespace

XplorerSidebarSectionLabel::XplorerSidebarSectionLabel(
    const std::u16string& text)
    : views::Label(text) {
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetFontList(font_list().DeriveWithSizeDelta(-1));
}

XplorerSidebarSectionLabel::~XplorerSidebarSectionLabel() = default;

gfx::Size XplorerSidebarSectionLabel::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = Label::CalculatePreferredSize(available_size);
  size.set_height(kSectionHeight);
  size.Enlarge(24, 12);
  return size;
}

BEGIN_METADATA(XplorerSidebarSectionLabel)
END_METADATA

}  // namespace xplorer