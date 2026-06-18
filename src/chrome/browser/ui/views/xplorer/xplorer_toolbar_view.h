// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessible_pane_view.h"
#include "ui/views/context_menu_controller.h"

class BrowserWindowInterface;
class Profile;
class GURL;

namespace views {
class MenuRunner;
class View;
}  // namespace views

namespace xplorer {

class XplorerToolbarPillButton;

// One entry in a pill's dropdown menu (mirrors toolbar.js children[]).
struct ToolbarChild {
  std::string label;
  std::string href;
};

// One toolbar pill. The schema mirrors companion/ui/toolbar.js DEFAULT_PILLS.
// |href| is either an absolute URL ("https://x.com/i/chat"), a gateway-relative
// path ("/search"), or the action sentinel "/switch-home?mode=web|build|wiki".
// |is_home| pills drive the profile-scoped /switch-home mode; |icon| is the
// toolbar.js icon id. |children| (when non-empty) gives the pill a dropdown.
struct ToolbarPill {
  std::string id;
  std::string label;
  std::string href;
  std::string icon;
  bool is_home = false;
  std::vector<ToolbarChild> children;
};

// Native browser-chrome strip of Grok pills. Lives in BrowserView's
// top_container_, between the bookmark bar and the contents separator. Window-
// scoped: always present on every tab. Pills are CONFIG-DRIVEN: read from
// ~/.xplorer/grok_settings.json (toolbar.pills) via grok_companion, with a
// built-in default set when the config is absent/empty.
class XplorerToolbarView : public views::AccessiblePaneView,
                           public ui::SimpleMenuModel::Delegate,
                           public views::ContextMenuController {
  METADATA_HEADER(XplorerToolbarView, views::AccessiblePaneView)

 public:
  XplorerToolbarView(BrowserWindowInterface* browser, Profile* profile);
  XplorerToolbarView(const XplorerToolbarView&) = delete;
  XplorerToolbarView& operator=(const XplorerToolbarView&) = delete;
  ~XplorerToolbarView() override;

  // Re-reads the pill config and rebuilds the buttons. Not yet wired to a
  // settings observer (later iteration); exposed so callers can refresh.
  void Reload();

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  // A pill's view(s): the main button, plus an optional trailing chevron button
  // present only when the pill has children. Index-aligned with |pills_|.
  struct PillViews {
    raw_ptr<XplorerToolbarPillButton> main = nullptr;
    raw_ptr<XplorerToolbarPillButton> chevron = nullptr;
  };

  // Reads toolbar.pills from grok_settings.json into |pills_|, falling back to
  // the built-in default set when the config is absent/empty.
  void LoadPills();
  // Builds the pill buttons (+ chevrons) for |pills_|, attaches icons, and
  // marks the active home pill as selected.
  void RebuildButtons();
  // Navigation dispatch for the pill at |pill_index|.
  void OnPillPressed(size_t pill_index);
  // Opens the children dropdown for the pill at |pill_index|, anchored to its
  // chevron (falling back to the main pill).
  void ShowChildMenu(size_t pill_index);
  // Navigates the active tab to |url| (shared by pills and menu items).
  void Navigate(const GURL& url);
  // Resolves a pill href to a GURL: a gateway-relative path ("/...") is
  // appended to the companion base URL; anything else is parsed as-is.
  GURL ResolveHref(const std::string& href) const;
  // Returns the pill index whose main button or chevron is |view|, or -1 for
  // the bar background.
  int PillIndexForView(const views::View* view) const;
  // Opens the toolbar customization surface (the /settings page).
  void OpenCustomizePage();

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<Profile> profile_;
  std::vector<ToolbarPill> pills_;
  // Index-aligned with |pills_|.
  std::vector<PillViews> pill_buttons_;

  // Child dropdown state; |open_pill_| is the pill whose menu is showing.
  std::unique_ptr<ui::SimpleMenuModel> child_menu_model_;
  std::unique_ptr<views::MenuRunner> child_menu_runner_;
  size_t open_pill_ = 0;

  // Right-click context-menu state; |context_pill_| is -1 for the bar
  // background or the index of the right-clicked pill.
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  int context_pill_ = -1;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_VIEW_H_
