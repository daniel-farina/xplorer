// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/views/xplorer/xplorer_scheduled_task_tabs.h"

#include "chrome/browser/agent_gateway/tab_ownership.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/xplorer/xplorer_bookmark_tabs.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace xplorer {

void HideScheduledTaskTabRow(Browser* browser, content::WebContents* wc) {
  if (!browser || !wc) {
    return;
  }
  agent_gateway::TabOwnership* own = agent_gateway::TabOwnership::Get(wc);
  if (!agent_gateway::IsScheduledTaskTab(own)) {
    return;
  }
  TabStripModel* tabs = browser->tab_strip_model();
  if (!tabs) {
    return;
  }
  if (tabs::TabInterface* tab = tabs->GetTabForWebContents(wc)) {
    SetTabRowVisible(browser, tab, false);
  }
}

void ReassertHiddenScheduledTaskTabRows(Browser* browser) {
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
    if (!agent_gateway::IsScheduledTaskTab(own)) {
      continue;
    }
    SetTabRowVisible(browser, tabs->GetTabAtIndex(i), false);
  }
}

}  // namespace xplorer