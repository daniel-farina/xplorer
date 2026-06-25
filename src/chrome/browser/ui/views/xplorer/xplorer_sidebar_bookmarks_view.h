// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_BOOKMARKS_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_BOOKMARKS_VIEW_H_

#include <set>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "url/gurl.h"
#include "components/bookmarks/browser/bookmark_model_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class BrowserWindowInterface;
class Profile;

namespace bookmarks {
class BookmarkModel;
class BookmarkNode;
}  // namespace bookmarks

namespace xplorer {

class XplorerSidebarRowButton;

// Bookmark-bar shortcuts rendered in the Arc-style sidebar.
class XplorerSidebarBookmarksView : public views::View,
                                    public bookmarks::BookmarkModelObserver,
                                    public views::ContextMenuController {
  METADATA_HEADER(XplorerSidebarBookmarksView, views::View)

 public:
  XplorerSidebarBookmarksView(BrowserWindowInterface* browser,
                              Profile* profile);
  XplorerSidebarBookmarksView(const XplorerSidebarBookmarksView&) = delete;
  XplorerSidebarBookmarksView& operator=(const XplorerSidebarBookmarksView&) =
      delete;
  ~XplorerSidebarBookmarksView() override;

  void Rebuild();

  // views::View:
  void OnThemeChanged() override;

  // bookmarks::BookmarkModelObserver:
  void BookmarkModelLoaded(bool ids_reassigned) override;
  void BookmarkNodeAdded(const bookmarks::BookmarkNode* parent,
                         size_t index,
                         bool added_by_user) override;
  void BookmarkNodeRemoved(const bookmarks::BookmarkNode* parent,
                           size_t old_index,
                           const bookmarks::BookmarkNode* node,
                           const std::set<GURL>& no_longer_bookmarked,
                           const base::Location& location) override;
  void BookmarkNodeChanged(const bookmarks::BookmarkNode* node) override;
  void BookmarkNodeMoved(const bookmarks::BookmarkNode* old_parent,
                         size_t old_index,
                         const bookmarks::BookmarkNode* new_parent,
                         size_t new_index) override;
  void BookmarkNodeFaviconChanged(const bookmarks::BookmarkNode* node) override;
  void BookmarkNodeChildrenReordered(
      const bookmarks::BookmarkNode* node) override;
  void BookmarkAllUserNodesRemoved(
      const std::set<GURL>& removed_urls,
      const base::Location& location) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void OnTabStripActiveTabChanged(BrowserWindowInterface* browser);
  void UpdateActiveHighlight();
  struct BookmarkRow {
    int64_t node_id = 0;
    GURL url;
    raw_ptr<XplorerSidebarRowButton> button = nullptr;
  };

  void AddBookmarkNodes(const bookmarks::BookmarkNode* parent, int depth);
  void OnBookmarkPressed(const bookmarks::BookmarkNode* node);
  void OnFolderPressed(const bookmarks::BookmarkNode* folder);
  void UpdateRowIcon(XplorerSidebarRowButton* button,
                     const bookmarks::BookmarkNode* node);
  XplorerSidebarRowButton* FindRowButton(int64_t node_id);
  gfx::Insets RowMargins(int depth) const;

  const raw_ptr<BrowserWindowInterface> browser_;
  const raw_ptr<Profile> profile_;
  raw_ptr<bookmarks::BookmarkModel> model_ = nullptr;
  std::vector<BookmarkRow> rows_;
  std::set<int64_t> expanded_folder_ids_;
  base::ScopedObservation<bookmarks::BookmarkModel,
                          bookmarks::BookmarkModelObserver>
      model_observation_{this};
  base::CallbackListSubscription active_tab_subscription_;
};

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SIDEBAR_BOOKMARKS_VIEW_H_