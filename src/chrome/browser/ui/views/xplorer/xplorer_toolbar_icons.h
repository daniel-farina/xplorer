// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_ICONS_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_ICONS_H_

#include <string_view>

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace xplorer {

// Maps a toolbar.js icon id ("globe", "chat", "wrench", ...) to a Chromium
// vector icon already compiled into the binary. Falls back to the link icon for
// unknown ids (mirrors companion/ui/toolbar.js ICONS.link default).
const gfx::VectorIcon& GetToolbarVectorIcon(std::string_view icon_id);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_ICONS_H_
