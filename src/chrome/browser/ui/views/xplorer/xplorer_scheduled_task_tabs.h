// Copyright 2026 The Xplorer Authors.
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SCHEDULED_TASK_TABS_H_
#define CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SCHEDULED_TASK_TABS_H_

class Browser;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace xplorer {

// Hides or shows a tab's row in the vertical tab strip. Used by the grouper's
// graduated-tab path (a tab that was a scheduled-task tab and is no longer).
void SetTabRowVisible(Browser* browser, tabs::TabInterface* tab, bool visible);

}  // namespace xplorer

#endif  // CHROME_BROWSER_UI_VIEWS_XPLORER_XPLORER_SCHEDULED_TASK_TABS_H_