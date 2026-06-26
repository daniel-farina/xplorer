// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SCHEDULED_TASK_TABS_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SCHEDULED_TASK_TABS_H_

class Browser;

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace xplorer {

// Hides or shows a tab's row in the vertical tab strip. Shared by the
// scheduled-task hiding path (and previously the Arc bookmark tabs).
void SetTabRowVisible(Browser* browser, tabs::TabInterface* tab, bool visible);

// Hides a scheduled-task tab row from the main Tabs strip (task tabs are owned
// by a background job, not user browsing).
void HideScheduledTaskTabRow(Browser* browser, content::WebContents* wc);

// Re-applies hidden rows for every tab stamped with TabOwnership::task_id.
void ReassertHiddenScheduledTaskTabRows(Browser* browser);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SCHEDULED_TASK_TABS_H_