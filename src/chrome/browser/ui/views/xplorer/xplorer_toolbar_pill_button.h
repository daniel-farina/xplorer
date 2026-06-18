// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_PILL_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_PILL_BUTTON_H_

#include <string_view>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/md_text_button.h"

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace xplorer {

// A compact rounded "pill" button for the native Xplorer toolbar. Subclasses
// MdTextButton following the OmniboxChipButton recipe (pill highlight path,
// custom padding, refresh-2023 ink drop) and adds:
//   * SetPillIcon() — a leading vector icon drawn at the toolbar icon color, and
//   * SetSelected() — an accent background marking the active pill.
// The default (unselected) pill carries a subtle toolbar background so the strip
// reads as a row of pills rather than flat text buttons.
class XplorerToolbarPillButton : public views::MdTextButton {
  METADATA_HEADER(XplorerToolbarPillButton, views::MdTextButton)

 public:
  explicit XplorerToolbarPillButton(
      PressedCallback callback = PressedCallback(),
      std::u16string_view text = std::u16string_view());
  XplorerToolbarPillButton(const XplorerToolbarPillButton&) = delete;
  XplorerToolbarPillButton& operator=(const XplorerToolbarPillButton&) = delete;
  ~XplorerToolbarPillButton() override;

  // Sets the leading vector icon, rendered at the toolbar icon color.
  void SetPillIcon(const gfx::VectorIcon& icon);

  // Marks this pill as the active/selected one (accent background).
  void SetSelected(bool selected);
  bool GetSelected() const { return selected_; }

  // Shows an integrated trailing dropdown caret inside the pill (reserves room
  // on the right). The owner routes clicks: see PointInCaret().
  void SetHasDropdownCaret(bool has_caret);
  bool GetHasDropdownCaret() const { return has_caret_; }

  // True if |point| (in this view's coordinates) falls in the trailing caret
  // hit-zone, so the owner can open the dropdown instead of navigating.
  bool PointInCaret(const gfx::Point& point) const;

  // views::MdTextButton:
  void OnThemeChanged() override;
  void UpdateBackgroundColor() override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  void UpdateIconImage();

  bool selected_ = false;
  bool has_caret_ = false;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_PILL_BUTTON_H_
