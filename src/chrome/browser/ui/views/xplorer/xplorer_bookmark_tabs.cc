// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tabs.h"

#include <map>
#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace xplorer {

namespace {

bool IsBookmarkOwnership(const agent_gateway::TabOwnership* own) {
  return own && own->bookmark_node_id != 0;
}

void TagBookmarkTab(content::WebContents* wc,
                    const GURL& url,
                    int64_t bookmark_node_id) {
  if (!wc || !url.is_valid() || bookmark_node_id == 0) {
    return;
  }
  agent_gateway::TabOwnership* own =
      agent_gateway::TabOwnership::GetOrCreate(wc);
  own->bookmark_url = url;
  own->bookmark_node_id = bookmark_node_id;
}

void NavigateBookmarkTabToUrl(content::WebContents* wc, const GURL& url) {
  if (!wc || !url.is_valid()) {
    return;
  }
  if (wc->GetLastCommittedURL() == url) {
    return;
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  wc->GetController().LoadURLWithParams(params);
}

VerticalTabStripView* GetVerticalTabStripView(Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->vertical_tab_strip_region_view()) {
    return nullptr;
  }
  return static_cast<VerticalTabStripView*>(
      browser_view->vertical_tab_strip_region_view()->GetTabStripView());
}

void HideBookmarkTabRow(Browser* browser, tabs::TabInterface* tab) {
  SetTabRowVisible(browser, tab, false);
}

// Tracks the last non-bookmark tab so the Tabs strip selection stays put while
// bookmark content is in the frame.
class BookmarkTabStripHelper : public TabStripModelObserver {
 public:
  explicit BookmarkTabStripHelper(Browser* browser) : browser_(browser) {
    browser_->tab_strip_model()->AddObserver(this);
    SeedLastUserTab();
  }

  ~BookmarkTabStripHelper() override {
    if (browser_) {
      browser_->tab_strip_model()->RemoveObserver(this);
    }
  }

  static BookmarkTabStripHelper* GetOrCreate(Browser* browser) {
    if (!browser) {
      return nullptr;
    }
    static base::NoDestructor<
        std::map<Browser*, std::unique_ptr<BookmarkTabStripHelper>>>
        helpers;
    auto& map = *helpers;
    auto it = map.find(browser);
    if (it != map.end()) {
      return it->second.get();
    }
    auto helper = std::make_unique<BookmarkTabStripHelper>(browser);
    BookmarkTabStripHelper* raw = helper.get();
    map.emplace(browser, std::move(helper));
    return raw;
  }

  const tabs::TabInterface* last_user_tab() const { return last_user_tab_; }

  void RememberCurrentUserTab() {
    TabStripModel* tabs = browser_->tab_strip_model();
    if (!tabs) {
      return;
    }
    content::WebContents* active = tabs->GetActiveWebContents();
    if (!active || IsBookmarkWebContents(active)) {
      return;
    }
    last_user_tab_ = tabs->GetTabForWebContents(active);
  }

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    if (!selection.active_tab_changed() || !selection.new_tab) {
      return;
    }
    if (!IsBookmarkWebContents(selection.new_tab->GetContents())) {
      last_user_tab_ = selection.new_tab;
    }
  }

 private:
  void SeedLastUserTab() {
    TabStripModel* tabs = browser_->tab_strip_model();
    if (!tabs) {
      return;
    }
    for (int i = 0; i < tabs->count(); ++i) {
      content::WebContents* wc = tabs->GetWebContentsAt(i);
      if (wc && !IsBookmarkWebContents(wc)) {
        last_user_tab_ = tabs->GetTabAtIndex(i);
        return;
      }
    }
  }

  raw_ptr<Browser> browser_;
  raw_ptr<const tabs::TabInterface> last_user_tab_ = nullptr;
};

content::WebContents* FindBookmarkTab(TabStripModel* tabs,
                                      int64_t bookmark_node_id) {
  if (!tabs || bookmark_node_id == 0) {
    return nullptr;
  }
  for (int i = 0; i < tabs->count(); ++i) {
    content::WebContents* wc = tabs->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
    if (IsBookmarkOwnership(own) && own->bookmark_node_id == bookmark_node_id) {
      return wc;
    }
  }
  return nullptr;
}

content::WebContents* CreateBackgroundBookmarkTab(Browser* browser,
                                                  const GURL& url) {
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  return params.navigated_or_inserted_contents;
}

void ActivateBookmarkTab(Browser* browser,
                         TabStripModel* tabs,
                         content::WebContents* wc,
                         const GURL& url,
                         int64_t bookmark_node_id) {
  if (!browser || !tabs || !wc) {
    return;
  }
  BookmarkTabStripHelper::GetOrCreate(browser)->RememberCurrentUserTab();

  TagBookmarkTab(wc, url, bookmark_node_id);
  NavigateBookmarkTabToUrl(wc, url);

  if (tabs::TabInterface* tab = tabs->GetTabForWebContents(wc)) {
    HideBookmarkTabRow(browser, tab);
  }

  const int index = tabs->GetIndexOfWebContents(wc);
  if (index != TabStripModel::kNoTab) {
    tabs->ActivateTabAt(index);
  }

  if (tabs::TabInterface* tab = tabs->GetTabForWebContents(wc)) {
    HideBookmarkTabRow(browser, tab);
  }
  ReassertHiddenBookmarkTabRows(browser);
}

void UpdateBookmarkTabRowVisibility(Browser* browser,
                                    TabStripModel* tabs,
                                    content::WebContents* wc) {
  if (!browser || !tabs || !wc) {
    return;
  }
  agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
  if (!IsBookmarkOwnership(own)) {
    return;
  }
  tabs::TabInterface* tab = tabs->GetTabForWebContents(wc);
  if (!tab) {
    return;
  }
  HideBookmarkTabRow(browser, tab);
}

}  // namespace

void SetTabRowVisible(Browser* browser,
                      tabs::TabInterface* tab,
                      bool visible) {
  if (!browser || !tab) {
    return;
  }
  VerticalTabStripView* strip = GetVerticalTabStripView(browser);
  if (!strip) {
    return;
  }
  strip->SetTabRowVisible(tab->GetHandle(), visible);
}

void OpenBookmarkTab(BrowserWindowInterface* browser,
                     const GURL& url,
                     int64_t bookmark_node_id) {
  if (!browser || !url.is_valid() || bookmark_node_id == 0) {
    return;
  }
  TabStripModel* tabs = browser->GetTabStripModel();
  Browser* target = browser->GetBrowserForMigrationOnly();
  if (!tabs || !target) {
    return;
  }

  BookmarkTabStripHelper::GetOrCreate(target);

  if (content::WebContents* existing =
          FindBookmarkTab(tabs, bookmark_node_id)) {
    ActivateBookmarkTab(target, tabs, existing, url, bookmark_node_id);
    return;
  }

  content::WebContents* wc = CreateBackgroundBookmarkTab(target, url);
  if (!wc) {
    return;
  }
  ActivateBookmarkTab(target, tabs, wc, url, bookmark_node_id);
}

bool IsBookmarkWebContents(content::WebContents* wc) {
  return IsBookmarkOwnership(agent_gateway::TabOwnership::Get(wc));
}

bool IsBookmarkTabOnUrl(content::WebContents* wc, const GURL& bookmark_url) {
  if (!wc || !bookmark_url.is_valid()) {
    return false;
  }
  return wc->GetLastCommittedURL() == bookmark_url;
}

bool IsBookmarkTabForNode(content::WebContents* wc, int64_t bookmark_node_id) {
  if (!wc || bookmark_node_id == 0) {
    return false;
  }
  agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
  return own && own->bookmark_node_id == bookmark_node_id;
}

bool ShouldShowAsActiveInStrip(tabs::TabInterface* tab) {
  if (!tab) {
    return false;
  }
  BrowserWindowInterface* bwi = tab->GetBrowserWindowInterface();
  TabStripModel* tabs = bwi ? bwi->GetTabStripModel() : nullptr;
  if (!tabs) {
    return false;
  }
  const int index = tabs->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  content::WebContents* active_wc = tabs->GetActiveWebContents();
  if (!IsBookmarkWebContents(active_wc)) {
    return tabs->IsTabInForeground(index);
  }
  Browser* browser = bwi->GetBrowserForMigrationOnly();
  BookmarkTabStripHelper* helper =
      browser ? BookmarkTabStripHelper::GetOrCreate(browser) : nullptr;
  return helper && helper->last_user_tab() == tab;
}

void ReassertHiddenBookmarkTabRows(Browser* browser) {
  if (!browser) {
    return;
  }
  TabStripModel* tabs = browser->tab_strip_model();
  if (!tabs) {
    return;
  }
  for (int i = 0; i < tabs->count(); ++i) {
    content::WebContents* wc = tabs->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    UpdateBookmarkTabRowVisibility(browser, tabs, wc);
  }
}

}  // namespace xplorer