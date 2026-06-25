// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_pill_button.h"

#include <utility>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/location_bar/location_bar_util.h"
#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/style/typography.h"

namespace xplorer {

namespace {

// Compact-pill geometry. ~28px tall with a 14px radius reads as a full capsule.
constexpr int kCornerRadius = 14;
constexpr int kIconSize = 16;
constexpr int kImageLabelSpacing = 6;
constexpr int kVerticalPadding = 4;
constexpr int kHorizontalPadding = 10;

// Integrated dropdown caret drawn inside the trailing edge of the pill.
constexpr int kCaretSize = 12;
constexpr int kCaretGap = 4;
constexpr int kCaretReserve = kCaretSize + kCaretGap;

}  // namespace

XplorerToolbarPillButton::XplorerToolbarPillButton(PressedCallback callback,
                                                   std::u16string_view text)
    : MdTextButton(std::move(callback), text, views::style::CONTEXT_BUTTON_MD) {
  // OmniboxChipButton recipe: rounded pill highlight, compact custom padding,
  // emphasized small label. Colors/ink drop are applied in OnThemeChanged()
  // once a color provider is available.
  views::InstallPillHighlightPathGenerator(this);
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetImageLabelSpacing(kImageLabelSpacing);
  SetCustomPadding(gfx::Insets::VH(kVerticalPadding, kHorizontalPadding));
  SetCornerRadius(kCornerRadius);
  label()->SetTextStyle(views::style::STYLE_BODY_4_EMPHASIS);
  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
}

XplorerToolbarPillButton::~XplorerToolbarPillButton() = default;

void XplorerToolbarPillButton::SetPillIcon(const gfx::VectorIcon& icon) {
  icon_ = &icon;
  using_favicon_ = false;
  UpdateIconImage();
}

void XplorerToolbarPillButton::SetPillFavicon(const ui::ImageModel& favicon) {
  if (!sidebar_row_style_ || favicon.IsEmpty()) {
    return;
  }
  using_favicon_ = true;
  SetImageModel(views::Button::STATE_NORMAL, favicon);
}

void XplorerToolbarPillButton::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  UpdateBackgroundColor();
  SchedulePaint();
}

void XplorerToolbarPillButton::SetSidebarRowStyle(bool sidebar_row) {
  if (sidebar_row_style_ == sidebar_row) {
    return;
  }
  sidebar_row_style_ = sidebar_row;
  if (sidebar_row) {
    SetImageLabelSpacing(8);
    SetCornerRadius(
        GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius));
    label()->SetTextStyle(views::style::STYLE_BODY_4);
  } else {
    using_favicon_ = false;
    SetImageLabelSpacing(kImageLabelSpacing);
    SetCornerRadius(kCornerRadius);
    label()->SetTextStyle(views::style::STYLE_BODY_4_EMPHASIS);
  }
  UpdatePadding();
  if (GetColorProvider()) {
    SetEnabledTextColors(sidebar_row_style_
                             ? kColorTabForegroundInactiveFrameInactive
                             : kColorToolbarText);
  }
  UpdateBackgroundColor();
  PreferredSizeChanged();
}

gfx::Size XplorerToolbarPillButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = MdTextButton::CalculatePreferredSize(available_size);
  if (sidebar_row_style_) {
    size.set_height(GetLayoutConstant(LayoutConstant::kVerticalTabHeight));
    if (available_size.width().is_bounded()) {
      size.set_width(available_size.width().value());
    }
  }
  return size;
}

void XplorerToolbarPillButton::OnThemeChanged() {
  MdTextButton::OnThemeChanged();
  SetEnabledTextColors(sidebar_row_style_
                           ? kColorTabForegroundInactiveFrameInactive
                           : kColorToolbarText);
  ConfigureInkDropForRefresh2023(this, kColorToolbarInkDropHover,
                                 kColorToolbarInkDropRipple);
  UpdateIconImage();
  UpdateBackgroundColor();
}

void XplorerToolbarPillButton::UpdateBackgroundColor() {
  const ui::ColorProvider* color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }
  const int radius =
      sidebar_row_style_
          ? GetLayoutConstant(LayoutConstant::kVerticalTabCornerRadius)
          : kCornerRadius;
  if (sidebar_row_style_) {
    if (!selected_) {
      SetBackground(nullptr);
      return;
    }
    SetBackground(views::CreateRoundedRectBackground(
        color_provider->GetColor(kColorTabBackgroundSelectedFrameInactive),
        radius));
    return;
  }
  const SkColor background =
      selected_
          ? color_provider->GetColor(
                kColorToolbarButtonBackgroundHighlightedDefault)
          : color_provider->GetColor(kColorToolbarBackgroundSubtleEmphasis);
  SetBackground(views::CreateRoundedRectBackground(background, radius));
}

void XplorerToolbarPillButton::UpdateIconImage() {
  if (using_favicon_ || !icon_ || !GetColorProvider()) {
    return;
  }
  const SkColor icon_color = sidebar_row_style_
                                 ? kColorTabForegroundInactiveFrameInactive
                                 : kColorToolbarButtonIcon;
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(*icon_, icon_color, kIconSize));
}

void XplorerToolbarPillButton::SetHasDropdownCaret(bool has_caret) {
  if (has_caret_ == has_caret) {
    return;
  }
  has_caret_ = has_caret;
  UpdatePadding();
  PreferredSizeChanged();
  SchedulePaint();
}

void XplorerToolbarPillButton::UpdatePadding() {
  const int vertical = sidebar_row_style_ ? 5 : kVerticalPadding;
  const int horizontal = sidebar_row_style_ ? 8 : kHorizontalPadding;
  SetCustomPadding(gfx::Insets::TLBR(
      vertical, horizontal, vertical,
      horizontal + (has_caret_ ? kCaretReserve : 0)));
}

bool XplorerToolbarPillButton::PointInCaret(const gfx::Point& point) const {
  if (!has_caret_) {
    return false;
  }
  // The trailing strip (caret + its surrounding padding) is the dropdown zone.
  const int zone_left = width() - kCaretReserve - kHorizontalPadding;
  return point.x() >= GetMirroredXInView(zone_left);
}

void XplorerToolbarPillButton::PaintButtonContents(gfx::Canvas* canvas) {
  MdTextButton::PaintButtonContents(canvas);
  if (!has_caret_ || !GetColorProvider()) {
    return;
  }
  const gfx::ImageSkia caret =
      ui::ImageModel::FromVectorIcon(GetToolbarVectorIcon("caret"),
                                     kColorToolbarButtonIcon, kCaretSize)
          .Rasterize(GetColorProvider());
  const int x = width() - kHorizontalPadding - kCaretSize;
  const int y = (height() - kCaretSize) / 2;
  canvas->DrawImageInt(caret, GetMirroredXWithWidthInView(x, kCaretSize), y);
}

BEGIN_METADATA(XplorerToolbarPillButton)
END_METADATA

}  // namespace xplorer
