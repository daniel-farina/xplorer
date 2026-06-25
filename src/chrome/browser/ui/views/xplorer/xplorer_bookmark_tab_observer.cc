// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tab_observer.h"

#include <memory>

#include "base/strings/string_util.h"
#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tabs.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace xplorer {

namespace {

constexpr char kBookmarkTabObserverKey[] = "xplorer_bookmark_tab_observer";

bool SameBookmarkHost(const GURL& a, const GURL& b) {
  return a.is_valid() && b.is_valid() && !a.host().empty() &&
         a.host() == b.host();
}

}  // namespace

XplorerBookmarkTabObserver::XplorerBookmarkTabObserver(
    Browser* browser,
    content::WebContents* contents)
    : content::WebContentsObserver(contents), browser_(browser) {}

XplorerBookmarkTabObserver::~XplorerBookmarkTabObserver() = default;

// static
void XplorerBookmarkTabObserver::ObserveBookmarkTab(
    Browser* browser,
    content::WebContents* wc) {
  if (!browser || !wc) {
    return;
  }
  if (wc->GetUserData(kBookmarkTabObserverKey)) {
    return;
  }
  wc->SetUserData(
      kBookmarkTabObserverKey,
      std::make_unique<XplorerBookmarkTabObserver>(browser, wc));
}

void XplorerBookmarkTabObserver::PrimaryPageChanged(content::Page& /*page*/) {
  content::WebContents* wc = web_contents();
  if (!wc || !browser_) {
    return;
  }
  agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
  if (!own || !own->bookmark_url.is_valid()) {
    return;
  }
  if (SameBookmarkHost(wc->GetLastCommittedURL(), own->bookmark_url)) {
    return;
  }
  own->bookmark_url = GURL();
  TabStripModel* tabs = browser_->tab_strip_model();
  if (tabs) {
    if (tabs::TabInterface* tab = tabs->GetTabForWebContents(wc)) {
      SetTabRowVisible(browser_, tab, true);
    }
  }
}

}  // namespace xplorer