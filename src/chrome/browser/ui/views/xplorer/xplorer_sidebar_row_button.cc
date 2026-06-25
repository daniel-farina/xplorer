// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_row_button.h"

#include <utility>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/location_bar/location_bar_util.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/style/typography.h"

namespace xplorer {

namespace {
constexpr int kImageLabelSpacing = 8;
constexpr int kVerticalPadding = 5;
constexpr int kHorizontalPadding = 8;
}  // namespace

XplorerSidebarRowButton::XplorerSidebarRowButton(PressedCallback callback,
                                                 std::u16string_view text)
    : views::MdTextButton(std::move(callback), text,
                            views::style::CONTEXT_BUTTON_MD) {
  views::InstallPillHighlightPathGenerator(this);
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetImageLabelSpacing(kImageLabelSpacing);
  SetCustomPadding(
      gfx::Insets::VH(kVerticalPadding, kHorizontalPadding));
  SetCornerRadius(
      GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius));
  label()->SetTextStyle(views::style::STYLE_BODY_4);
  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
}

XplorerSidebarRowButton::~XplorerSidebarRowButton() = default;

void XplorerSidebarRowButton::SetRowIcon(const ui::ImageModel& icon) {
  SetImageModel(views::Button::STATE_NORMAL, icon);
}

void XplorerSidebarRowButton::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  UpdateBackgroundColor();
  SchedulePaint();
}

gfx::Size XplorerSidebarRowButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = MdTextButton::CalculatePreferredSize(available_size);
  size.set_height(GetLayoutConstant(LayoutConstant::kVerticalTabHeight));
  if (available_size.width().is_bounded()) {
    size.set_width(available_size.width().value());
  }
  return size;
}

void XplorerSidebarRowButton::OnThemeChanged() {
  MdTextButton::OnThemeChanged();
  SetEnabledTextColors(kColorTabForegroundInactiveFrameInactive);
  ConfigureInkDropForRefresh2023(this, kColorToolbarInkDropHover,
                                 kColorToolbarInkDropRipple);
  UpdateBackgroundColor();
}

void XplorerSidebarRowButton::UpdateBackgroundColor() {
  const ui::ColorProvider* color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }
  const SkColor background =
      selected_
          ? color_provider->GetColor(
                kColorToolbarButtonBackgroundHighlightedDefault)
          : color_provider->GetColor(kColorToolbarBackgroundSubtleEmphasis);
  SetBackground(views::CreateRoundedRectBackground(
      background, GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius)));
}

BEGIN_METADATA(XplorerSidebarRowButton)
END_METADATA

}  // namespace xplorer