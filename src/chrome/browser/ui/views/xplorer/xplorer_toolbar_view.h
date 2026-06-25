// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_VIEW_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-forward.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessible_pane_view.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/drag_controller.h"

class BrowserWindowInterface;
class Profile;
class GURL;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace ui {
class ClipboardFormatType;
class DropTargetEvent;
class Event;
class LayerTreeOwner;
class OSExchangeData;
}  // namespace ui

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
                           public views::ContextMenuController,
                           public views::DragController,
                           public content::WebContentsObserver {
  METADATA_HEADER(XplorerToolbarView, views::AccessiblePaneView)

 public:
  XplorerToolbarView(BrowserWindowInterface* browser, Profile* profile);
  XplorerToolbarView(const XplorerToolbarView&) = delete;
  XplorerToolbarView& operator=(const XplorerToolbarView&) = delete;
  ~XplorerToolbarView() override;

  // Re-reads the pill config and rebuilds the buttons. Not yet wired to a
  // settings observer (later iteration); exposed so callers can refresh.
  void Reload();

  // When true, pills stack vertically for the Arc-style sidebar rail.
  void SetVerticalLayout(bool vertical);
  bool vertical_layout() const { return vertical_layout_; }

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

  // views::View (drag drop target — reorder within the strip):
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool AreDropTypesRequired() override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;

  // views::DragController (drag source — each pill button):
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

 protected:
  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  // A pill's view: a single styled button. Pills with children render an
  // integrated trailing dropdown caret inside this same button (no separate
  // chevron view). Index-aligned with |pills_|.
  struct PillViews {
    raw_ptr<XplorerToolbarPillButton> main = nullptr;
  };

  // Reads toolbar.pills from grok_settings.json into |pills_|, falling back to
  // the built-in default set when the config is absent/empty.
  void LoadPills();
  // Builds the pill buttons (+ chevrons) for |pills_|, attaches icons, and
  // marks the active home pill as selected.
  void RebuildButtons();
  void ApplyVerticalButtonChrome();
  // Click dispatch for a pill: opens the dropdown when the press lands in the
  // integrated caret zone (and the pill has children), else navigates.
  void OnPillActivated(size_t pill_index, const ui::Event& event);
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

  // Highlights the pill that best matches the active tab's current page, so the
  // highlight tracks the foreground page rather than the persisted search-home
  // mode. Re-run on tab switches and navigations.
  void UpdateActiveHighlight();
  // RegisterActiveTabDidChange callback: re-observe the newly-active tab's
  // WebContents and refresh the highlight.
  void OnActiveTabChanged(BrowserWindowInterface* browser);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  // Drag-reorder helpers.
  // Returns the insertion index (0..pills_.size()) for a cursor at local |x|.
  int ComputeDropIndex(int x) const;
  // Serializes |pills_| (current order, all fields) to pill dicts.
  std::vector<base::DictValue> SerializePills() const;
  // Moves the pill with |dragged_id| to |target_index|, persists, reloads.
  void PerformDrop(std::string dragged_id,
                   int target_index,
                   const ui::DropTargetEvent& event,
                   ui::mojom::DragOperation& output_drag_op,
                   std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner);

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

  // Reloads the bar when toolbar config is persisted (settings page / gateway).
  base::CallbackListSubscription toolbar_config_subscription_;

  // Fires on active-tab switches so the highlight can follow the foreground tab.
  base::CallbackListSubscription active_tab_subscription_;

  // Drag-reorder state. |drop_index_| is the live insertion marker during a
  // drag (-1 when not dragging), used to paint the drop indicator.
  int drop_index_ = -1;

  bool vertical_layout_ = false;

  base::WeakPtrFactory<XplorerToolbarView> weak_factory_{this};
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_TOOLBAR_VIEW_H_
