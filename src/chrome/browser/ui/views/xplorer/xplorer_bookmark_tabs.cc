// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tabs.h"

#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tab_observer.h"

#include "base/strings/string_util.h"
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
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace xplorer {

namespace {

bool SameBookmarkHost(const GURL& a, const GURL& b) {
  return a.is_valid() && b.is_valid() && !a.host().empty() &&
         a.host() == b.host();
}

void TagBookmarkTab(content::WebContents* wc, const GURL& url) {
  if (!wc || !url.is_valid()) {
    return;
  }
  agent_gateway::TabOwnership* own =
      agent_gateway::TabOwnership::GetOrCreate(wc);
  own->bookmark_url = url;
}

void ClearBookmarkTabTag(content::WebContents* wc) {
  if (!wc) {
    return;
  }
  if (agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc)) {
    own->bookmark_url = GURL();
  }
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

void OpenBookmarkTab(BrowserWindowInterface* browser, const GURL& url) {
  if (!browser || !url.is_valid()) {
    return;
  }
  TabStripModel* tabs = browser->GetTabStripModel();
  if (!tabs) {
    return;
  }

  for (int i = 0; i < tabs->count(); ++i) {
    content::WebContents* wc = tabs->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
    if (own && own->bookmark_url == url) {
      tabs->ActivateTabAt(i);
      XplorerBookmarkTabObserver::ObserveBookmarkTab(
          browser->GetBrowserForMigrationOnly(), wc);
      SetTabRowVisible(browser->GetBrowserForMigrationOnly(),
                       tabs->GetTabAtIndex(i), false);
      return;
    }
    if (wc->GetLastCommittedURL() == url) {
      TagBookmarkTab(wc, url);
      tabs->ActivateTabAt(i);
      XplorerBookmarkTabObserver::ObserveBookmarkTab(
          browser->GetBrowserForMigrationOnly(), wc);
      SetTabRowVisible(browser->GetBrowserForMigrationOnly(),
                       tabs->GetTabAtIndex(i), false);
      return;
    }
  }

  Browser* target = browser->GetBrowserForMigrationOnly();
  if (!target) {
    return;
  }
  NavigateParams params(target, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  content::WebContents* wc = params.navigated_or_inserted_contents;
  if (!wc) {
    return;
  }
  TagBookmarkTab(wc, url);
  XplorerBookmarkTabObserver::ObserveBookmarkTab(target, wc);
  if (tabs::TabInterface* tab = tabs->GetTabForWebContents(wc)) {
    SetTabRowVisible(target, tab, false);
  }
}

bool IsBookmarkTabOnUrl(content::WebContents* wc, const GURL& bookmark_url) {
  if (!wc || !bookmark_url.is_valid()) {
    return false;
  }
  agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
  if (!own || own->bookmark_url != bookmark_url) {
    return false;
  }
  return SameBookmarkHost(wc->GetLastCommittedURL(), bookmark_url);
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
    agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
    if (!own || !own->bookmark_url.is_valid()) {
      continue;
    }
    const bool hide =
        SameBookmarkHost(wc->GetLastCommittedURL(), own->bookmark_url);
    SetTabRowVisible(browser, tabs->GetTabAtIndex(i), !hide);
    if (!hide) {
      ClearBookmarkTabTag(wc);
    }
  }
}

}  // namespace xplorer