// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_pill_button.h"

#include <utility>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/location_bar/location_bar_util.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
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
  UpdateIconImage();
}

void XplorerToolbarPillButton::SetSelected(bool selected) {
  if (selected_ == selected) {
    return;
  }
  selected_ = selected;
  UpdateBackgroundColor();
  SchedulePaint();
}

void XplorerToolbarPillButton::OnThemeChanged() {
  MdTextButton::OnThemeChanged();
  SetEnabledTextColors(kColorToolbarText);
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
  const SkColor background =
      selected_
          ? color_provider->GetColor(
                kColorToolbarButtonBackgroundHighlightedDefault)
          : color_provider->GetColor(kColorToolbarBackgroundSubtleEmphasis);
  SetBackground(views::CreateRoundedRectBackground(background, kCornerRadius));
}

void XplorerToolbarPillButton::UpdateIconImage() {
  if (!icon_ || !GetColorProvider()) {
    return;
  }
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(*icon_, kColorToolbarButtonIcon,
                                               kIconSize));
}

BEGIN_METADATA(XplorerToolbarPillButton)
END_METADATA

}  // namespace xplorer
