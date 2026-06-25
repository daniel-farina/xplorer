// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_PREFS_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_PREFS_H_

namespace xplorer {

enum class ToolbarPlacement {
  kTop,
  kSidebar,
};

// Reads toolbar.placement from ~/.xplorer/grok_settings.json.
// Defaults to kSidebar so the Arc-style rail is the out-of-box experience.
ToolbarPlacement GetToolbarPlacement();

// Persists toolbar.placement ("top" | "sidebar") to grok_settings.json.
void SetToolbarPlacement(ToolbarPlacement placement);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_PREFS_H_