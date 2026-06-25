// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_ROW_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_ROW_BUTTON_H_

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/views/controls/button/md_text_button.h"

namespace xplorer {

// Tab-strip row used in the sidebar for bookmarks and Grok pills — matches
// VerticalTabView geometry (icon + left-aligned title, rounded hover bg).
class XplorerSidebarRowButton : public views::MdTextButton {
  METADATA_HEADER(XplorerSidebarRowButton, views::MdTextButton)

 public:
  explicit XplorerSidebarRowButton(PressedCallback callback,
                                   std::u16string_view text);
  XplorerSidebarRowButton(const XplorerSidebarRowButton&) = delete;
  XplorerSidebarRowButton& operator=(const XplorerSidebarRowButton&) = delete;
  ~XplorerSidebarRowButton() override;

  void SetRowIcon(const ui::ImageModel& icon);
  void SetRowTitle(std::u16string_view title);
  // Folder rows show a leading expand/collapse chevron in the title.
  void SetFolderStyle(bool is_folder, bool expanded);
  void SetSelected(bool selected);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 protected:
  void OnThemeChanged() override;
  void UpdateBackgroundColor() override;

 private:
  void RefreshTitle();

  std::u16string base_title_;
  bool is_folder_ = false;
  bool folder_expanded_ = false;
  bool selected_ = false;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_ROW_BUTTON_H_