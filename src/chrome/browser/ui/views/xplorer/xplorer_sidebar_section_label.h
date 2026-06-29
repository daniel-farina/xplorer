// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_SECTION_LABEL_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_SECTION_LABEL_H_

#include <string>

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"

namespace xplorer {

// Small section header ("Bookmarks", "Tabs") for the Arc-style sidebar.
class XplorerSidebarSectionLabel : public views::Label {
  METADATA_HEADER(XplorerSidebarSectionLabel, views::Label)

 public:
  explicit XplorerSidebarSectionLabel(const std::u16string& text);
  XplorerSidebarSectionLabel(const XplorerSidebarSectionLabel&) = delete;
  XplorerSidebarSectionLabel& operator=(const XplorerSidebarSectionLabel&) =
      delete;
  ~XplorerSidebarSectionLabel() override;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_SECTION_LABEL_H_