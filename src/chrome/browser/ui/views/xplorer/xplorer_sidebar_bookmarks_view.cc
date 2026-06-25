// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_bookmarks_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_row_button.h"
#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_section_label.h"
#include "chrome/browser/ui/views/xplorer/xplorer_toolbar_view.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace xplorer {

namespace {
constexpr int kMaxBookmarks = 12;

gfx::Insets SidebarRowMargins() {
  const int h =
      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);
  return gfx::Insets::TLBR(2, h, 2, h);
}
}  // namespace

XplorerSidebarBookmarksView::XplorerSidebarBookmarksView(
    BrowserWindowInterface* browser,
    Profile* profile)
    : browser_(browser), profile_(profile) {
  set_context_menu_controller(this);
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
  Rebuild();
}

void XplorerSidebarBookmarksView::BookmarkNodeChanged(
    const bookmarks::BookmarkNode* node) {
  if (XplorerSidebarRowButton* button = FindRowButton(node->id())) {
    button->SetText(node->GetTitle());
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

void XplorerSidebarBookmarksView::Rebuild() {
  RemoveAllChildViews();
  rows_.clear();
  if (!model_ || !model_->loaded()) {
    return;
  }

  auto* header = AddChildView(std::make_unique<XplorerSidebarSectionLabel>(
      u"Bookmarks"));
  header->SetProperty(views::kMarginsKey,
                      gfx::Insets::TLBR(4, SidebarRowMargins().left(), 0, 0));

  const bookmarks::BookmarkNode* bar = model_->bookmark_bar_node();
  int shown = 0;
  for (const auto& child : bar->children()) {
    if (!child->is_url()) {
      continue;
    }
    auto button = std::make_unique<XplorerSidebarRowButton>(
        base::BindRepeating(&XplorerSidebarBookmarksView::OnBookmarkPressed,
                            base::Unretained(this), child.get()),
        child->GetTitle());
    button->SetProperty(views::kMarginsKey, SidebarRowMargins());
    UpdateRowIcon(button.get(), child.get());
    XplorerSidebarRowButton* row =
        AddChildView(std::move(button));
    rows_.push_back({child->id(), row});
    if (++shown >= kMaxBookmarks) {
      break;
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
  ui::ImageModel icon =
      image.IsEmpty()
          ? favicon::GetDefaultFaviconModel(kColorBookmarkBarBackground)
          : ui::ImageModel::FromImage(image);
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
  TabStripModel* tabs = browser_->GetTabStripModel();
  if (!tabs) {
    return;
  }
  content::WebContents* contents = tabs->GetActiveWebContents();
  if (!contents) {
    return;
  }
  contents->GetController().LoadURL(node->url(), content::Referrer(),
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                    std::string());
}

BEGIN_METADATA(XplorerSidebarBookmarksView)
END_METADATA

}  // namespace xplorer