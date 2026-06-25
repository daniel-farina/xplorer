// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_bookmarks_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tabs.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_row_button.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_section_label.h"
#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_view.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace xplorer {

namespace {
constexpr int kFolderIndentPx = 12;
}  // namespace

XplorerSidebarBookmarksView::XplorerSidebarBookmarksView(
    BrowserWindowInterface* browser,
    Profile* profile)
    : browser_(browser), profile_(profile) {
  set_context_menu_controller(this);
  SetBackground(nullptr);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  model_ = BookmarkModelFactory::GetForBrowserContext(profile_.get());
  if (model_) {
    model_observation_.Observe(model_);
    if (model_->loaded()) {
      Rebuild();
    }
  }
  if (browser_) {
    active_tab_subscription_ = browser_->RegisterActiveTabDidChange(
        base::BindRepeating(&XplorerSidebarBookmarksView::OnTabStripActiveTabChanged,
                            base::Unretained(this)));
    OnTabStripActiveTabChanged(browser_);
  }
}

XplorerSidebarBookmarksView::~XplorerSidebarBookmarksView() = default;

void XplorerSidebarBookmarksView::BookmarkModelLoaded(bool ids_reassigned) {
  Rebuild();
}

void XplorerSidebarBookmarksView::BookmarkNodeAdded(
    const bookmarks::BookmarkNode* parent,
    size_t index,
    bool added_by_user) {
  Rebuild();
}

void XplorerSidebarBookmarksView::BookmarkNodeRemoved(
    const bookmarks::BookmarkNode* parent,
    size_t old_index,
    const bookmarks::BookmarkNode* node,
    const std::set<GURL>& no_longer_bookmarked,
    const base::Location& location) {
  expanded_folder_ids_.erase(node->id());
  Rebuild();
}

void XplorerSidebarBookmarksView::BookmarkNodeChanged(
    const bookmarks::BookmarkNode* node) {
  if (XplorerSidebarRowButton* button = FindRowButton(node->id())) {
    button->SetRowTitle(node->GetTitle());
  } else {
    Rebuild();
  }
}

void XplorerSidebarBookmarksView::BookmarkNodeMoved(
    const bookmarks::BookmarkNode* old_parent,
    size_t old_index,
    const bookmarks::BookmarkNode* new_parent,
    size_t new_index) {
  Rebuild();
}

void XplorerSidebarBookmarksView::BookmarkNodeFaviconChanged(
    const bookmarks::BookmarkNode* node) {
  if (XplorerSidebarRowButton* button = FindRowButton(node->id())) {
    UpdateRowIcon(button, node);
  }
}

void XplorerSidebarBookmarksView::BookmarkNodeChildrenReordered(
    const bookmarks::BookmarkNode* node) {
  Rebuild();
}

void XplorerSidebarBookmarksView::BookmarkAllUserNodesRemoved(
    const std::set<GURL>& removed_urls,
    const base::Location& location) {
  expanded_folder_ids_.clear();
  Rebuild();
}

void XplorerSidebarBookmarksView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!browser_) {
    return;
  }
  Browser* b = browser_->GetBrowserForMigrationOnly();
  if (!b) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(b);
  XplorerToolbarView* toolbar =
      browser_view ? browser_view->xplorer_toolbar() : nullptr;
  if (toolbar) {
    toolbar->ShowContextMenuForViewImpl(source, point, source_type);
  }
}

void XplorerSidebarBookmarksView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(nullptr);
}

gfx::Insets XplorerSidebarBookmarksView::RowMargins(int depth) const {
  const int left = depth * kFolderIndentPx;
  return gfx::Insets::TLBR(2, left, 2, 0);
}

void XplorerSidebarBookmarksView::Rebuild() {
  RemoveAllChildViews();
  rows_.clear();
  if (!model_ || !model_->loaded()) {
    return;
  }

  auto* header = AddChildView(std::make_unique<XplorerSidebarSectionLabel>(
      u"Bookmarks"));
  header->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));

  AddBookmarkNodes(model_->bookmark_bar_node(), /*depth=*/0);
  UpdateActiveHighlight();
}

void XplorerSidebarBookmarksView::AddBookmarkNodes(
    const bookmarks::BookmarkNode* parent,
    int depth) {
  if (!parent) {
    return;
  }
  for (const auto& child : parent->children()) {
    if (child->is_url()) {
      auto button = std::make_unique<XplorerSidebarRowButton>(
          base::BindRepeating(&XplorerSidebarBookmarksView::OnBookmarkPressed,
                              base::Unretained(this), child.get()),
          child->GetTitle());
      button->SetProperty(views::kMarginsKey, RowMargins(depth));
      UpdateRowIcon(button.get(), child.get());
      XplorerSidebarRowButton* row = AddChildView(std::move(button));
      rows_.push_back({child->id(), child->url(), row});
      continue;
    }

    if (!child->is_folder()) {
      continue;
    }

    const bool expanded = expanded_folder_ids_.count(child->id()) > 0;
    auto folder_button = std::make_unique<XplorerSidebarRowButton>(
        base::BindRepeating(&XplorerSidebarBookmarksView::OnFolderPressed,
                            base::Unretained(this), child.get()),
        child->GetTitle());
    folder_button->SetProperty(views::kMarginsKey, RowMargins(depth));
    folder_button->SetFolderStyle(/*is_folder=*/true, expanded);
    folder_button->SetRowIcon(chrome::GetBookmarkFolderIcon(
        chrome::BookmarkFolderIconType::kNormal, ui::kColorMenuIcon));
    AddChildView(std::move(folder_button));

    if (expanded) {
      AddBookmarkNodes(child.get(), depth + 1);
    }
  }
}

void XplorerSidebarBookmarksView::UpdateRowIcon(
    XplorerSidebarRowButton* button,
    const bookmarks::BookmarkNode* node) {
  if (!button || !node || !model_) {
    return;
  }
  const gfx::Image& image = model_->GetFavicon(node);
  ui::ImageModel icon;
  if (image.IsEmpty()) {
    icon = favicon::GetDefaultFaviconModel(kColorBookmarkBarBackground);
  } else {
    const gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
        image.AsImageSkia(), skia::ImageOperations::RESIZE_BEST,
        gfx::Size(gfx::kFaviconSize, gfx::kFaviconSize));
    icon = ui::ImageModel::FromImageSkia(resized);
  }
  button->SetRowIcon(icon);
}

XplorerSidebarRowButton* XplorerSidebarBookmarksView::FindRowButton(
    int64_t node_id) {
  for (const BookmarkRow& row : rows_) {
    if (row.node_id == node_id) {
      return row.button;
    }
  }
  return nullptr;
}

void XplorerSidebarBookmarksView::OnBookmarkPressed(
    const bookmarks::BookmarkNode* node) {
  if (!node || !node->is_url() || !browser_) {
    return;
  }
  OpenBookmarkTab(browser_, node->url(), node->id());
  UpdateActiveHighlight();
}

void XplorerSidebarBookmarksView::OnFolderPressed(
    const bookmarks::BookmarkNode* folder) {
  if (!folder || !folder->is_folder()) {
    return;
  }
  if (expanded_folder_ids_.count(folder->id())) {
    expanded_folder_ids_.erase(folder->id());
  } else {
    expanded_folder_ids_.insert(folder->id());
  }
  Rebuild();
}

void XplorerSidebarBookmarksView::OnTabStripActiveTabChanged(
    BrowserWindowInterface* browser) {
  UpdateActiveHighlight();
}

void XplorerSidebarBookmarksView::UpdateActiveHighlight() {
  if (!browser_ || !model_) {
    return;
  }
  TabStripModel* tabs = browser_->GetTabStripModel();
  content::WebContents* active = tabs ? tabs->GetActiveWebContents() : nullptr;
  for (const BookmarkRow& row : rows_) {
    if (!row.button) {
      continue;
    }
    bool selected = false;
    if (active) {
      if (row.node_id != 0) {
        selected = IsBookmarkTabForNode(active, row.node_id);
      } else if (row.url.is_valid()) {
        selected = IsBookmarkTabOnUrl(active, row.url);
      }
    }
    row.button->SetSelected(selected);
  }
}

BEGIN_METADATA(XplorerSidebarBookmarksView)
END_METADATA

}  // namespace xplorer