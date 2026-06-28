#!/usr/bin/env python3
"""Applies Xplorer's integration edits to a Chromium checkout.

Idempotent: each edit checks for its marker before inserting. Anchors on
long-lived symbols (PreMainMessageLoopRun, chrome/browser deps block) so it
survives upstream churn better than context diffs.
"""
import re
import sys
from pathlib import Path

MARKER = "// XPLORER"


def edit(path: Path, anchor: str, insertion: str, before: bool = False):
    text = path.read_text()
    # Idempotency: skip if the full insertion is already present verbatim.
    if insertion in text:
        print(f"  skip (already applied): {path}")
        return
    idx = text.find(anchor)
    if idx < 0:
        sys.exit(f"ANCHOR NOT FOUND in {path}: {anchor!r} — upstream moved; "
                 f"update apply_integration.py")
    # When the insertion restates the anchor's first OR last line, it is a
    # rewritten version of the anchored block → replace the anchor with it.
    # Otherwise the insertion is purely additive → splice it before/after the
    # anchor. (Matching the last line catches rewrites whose opening line
    # changes but which end on the same statement, e.g. a trailing `return`.)
    a_lines = anchor.strip().splitlines()
    i_lines = insertion.strip().splitlines()
    if a_lines[0] == i_lines[0] or a_lines[-1] == i_lines[-1]:
        new_text = text[:idx] + insertion + text[idx + len(anchor):]
    else:
        pos = idx if before else idx + len(anchor)
        new_text = text[:pos] + insertion + text[pos:]
    path.write_text(new_text)
    print(f"  edited: {path}")


def rebrand_grd_strings(path: Path):
    """Replace the hardcoded "Chromium" app name with "Xplorer" in a grit
    strings file (.grd/.grdp), preserving the legal "Chromium Authors" copyright.
    Skips the google_chrome branded variants (not compiled in our Chromium build)
    and — as a safety net — any file whose <message name="…"> resource IDs contain
    "Chromium" (replacing those would break the build). Idempotent: once only the
    copyright keeps "Chromium", re-running is a no-op."""
    if not path.exists() or "google_chrome" in path.name:
        return
    g = path.read_text()
    if re.search(r'name="[^"]*Chromium', g):
        print(f"  skip (Chromium in resource IDs): {path.name}")
        return
    if g.count("Chromium") <= g.count("Chromium Authors"):
        return
    g = g.replace("Chromium Authors", "\x00A\x00")
    g = g.replace("Chromium", "Xplorer")
    g = g.replace("\x00A\x00", "Chromium Authors")
    path.write_text(g)
    print(f"  rebranded grd: {path.name}")


def patch_xplorer_settings_access(src: Path):
    """Xplorer settings in app menu, chrome://settings nav, and command handler."""
    cmd_ids = src / "chrome/app/chrome_command_ids.h"
    edit(
        cmd_ids,
        "#define IDC_CHROME_ENTERPRISE_RELEASE_NOTES 40305",
        "#define IDC_CHROME_ENTERPRISE_RELEASE_NOTES 40305\n"
        "#define IDC_XPLORER_SETTINGS 40306  // XPLORER",
    )

    grdp = src / "chrome/app/settings_chromium_strings.grdp"
    edit(
        grdp,
        "  <!-- About Page -->",
        '  <message name="IDS_XPLORER_SETTINGS" '
        'desc="App menu item to open Xplorer companion settings" '
        'translateable="false">\n'
        "    Xplorer settings\n"
        "  </message>\n\n"
        "  <!-- About Page -->",
    )

    app_menu = src / "chrome/browser/ui/toolbar/app_menu_model.cc"
    edit(
        app_menu,
        "  AddItemWithStringIdAndVectorIcon(\n"
        "      this, IDC_OPTIONS, IDS_SETTINGS,\n"
        "      features::IsRoundedIconsEnabled() ? kSettingsIcon : kSettingsMenuOldIcon);\n",
        "  AddItemWithStringIdAndVectorIcon(\n"
        "      this, IDC_OPTIONS, IDS_SETTINGS,\n"
        "      features::IsRoundedIconsEnabled() ? kSettingsIcon : kSettingsMenuOldIcon);\n\n"
        "  // XPLORER: companion settings (bookmarks, models, Grok defaults).\n"
        "  AddItemWithStringId(IDC_XPLORER_SETTINGS, IDS_XPLORER_SETTINGS);\n",
    )

    bcc = src / "chrome/browser/ui/browser_command_controller.cc"
    edit(
        bcc,
        '#include "chrome/browser/ui/browser_commands.h"',
        '#include "chrome/browser/ui/browser_commands.h"\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_settings_nav.h"  // XPLORER',
    )
    edit(
        bcc,
        "    case IDC_OPTIONS:\n"
        "      ShowSettings(browser_->GetBrowserForOpeningWebUi());\n"
        "      break;",
        "    case IDC_OPTIONS:\n"
        "      ShowSettings(browser_->GetBrowserForOpeningWebUi());\n"
        "      break;\n"
        "    case IDC_XPLORER_SETTINGS:  // XPLORER\n"
        "      xplorer::OpenXplorerSettings(browser_);\n"
        "      break;",
    )

    settings_menu = (
        src / "chrome/browser/resources/settings/settings_menu/settings_menu.html"
    )
    edit(
        settings_menu,
        '        <a role="menuitem" id="about-menu" href="/help"\n'
        '            class="cr-nav-menu-item">',
        '        <a role="menuitem" id="xplorer-settings-link" class="cr-nav-menu-item"\n'
        '            href="http://127.0.0.1:9334/settings" target="_blank"\n'
        '            on-click="onLinkClick_"\n'
        '            title="Bookmarks, models, and Grok defaults">\n'
        '          <cr-icon icon="settings:settings"></cr-icon>\n'
        '          <span>Xplorer settings</span>\n'
        '          <div class="cr-icon icon-external"></div>\n'
        '          <cr-ripple></cr-ripple>\n'
        '        </a>\n'
        '        <a role="menuitem" id="about-menu" href="/help"\n'
        '            class="cr-nav-menu-item">',
    )


def patch_vertical_sidebar(src: Path):
    """Arc-style sidebar chrome in the vertical tab strip.

    Injects XplorerSidebarChromeView (the "Tabs" section label) at the top of
    VerticalTabStripRegionView and auto-groups agent-owned tabs.
    """
    vts_h = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
    edit(
        vts_h,
        "class VerticalTabStripBottomContainer;",
        "class VerticalTabStripBottomContainer;\n"
        "namespace xplorer {\n"
        "class XplorerSidebarChromeView;\n"
        "}  // namespace xplorer  // XPLORER",
    )
    # Scheduled-section forward declaration in its OWN namespace block, spliced
    # after the chrome view's block. Important: do NOT add a line *inside* the
    # chrome view's block — that block is the verbatim insertion of the edit
    # above, and mutating it would break that edit's idempotency guard and cause
    # it to re-fire (duplicating the block) on the next apply.
    if "XplorerSidebarScheduledView" not in vts_h.read_text():
        edit(
            vts_h,
            "class XplorerSidebarChromeView;\n"
            "}  // namespace xplorer  // XPLORER",
            "class XplorerSidebarChromeView;\n"
            "}  // namespace xplorer  // XPLORER\n"
            "namespace xplorer {\n"
            "class XplorerSidebarScheduledView;\n"
            "}  // namespace xplorer  // XPLORER",
        )
    vts_h_text = vts_h.read_text()
    if "InstallXplorerSidebarChrome" not in vts_h_text:
        edit(
            vts_h,
            "  VerticalPinnedTabContainerView* GetPinnedTabsContainer();\n"
            "  VerticalUnpinnedTabContainerView* GetUnpinnedTabsContainer();\n\n"
            "  VerticalTabStripTopContainer* GetTopContainer() {\n"
            "    return top_button_container_;\n"
            "  }",
            "  VerticalPinnedTabContainerView* GetPinnedTabsContainer();\n"
            "  VerticalUnpinnedTabContainerView* GetUnpinnedTabsContainer();\n\n"
            "  // XPLORER: Arc-style sidebar chrome below the top menu bar.\n"
            "  void InstallXplorerSidebarChrome(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome);\n"
            "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
            "    return xplorer_sidebar_chrome_;\n"
            "  }\n\n"
            "  VerticalTabStripTopContainer* GetTopContainer() {\n"
            "    return top_button_container_;\n"
            "  }",
        )
    edit(
        vts_h,
        "  bool tab_strip_editable_for_testing_ = true;\n\n"
        "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;",
        "  bool tab_strip_editable_for_testing_ = true;\n\n"
        "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
        "      nullptr;  // XPLORER\n"
        "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;",
    )
    # Scheduled-section installer + accessor, mirroring the chrome view.
    if "InstallXplorerSidebarScheduled" not in vts_h.read_text():
        edit(
            vts_h,
            "  void InstallXplorerSidebarChrome(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome);\n"
            "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
            "    return xplorer_sidebar_chrome_;\n"
            "  }",
            "  void InstallXplorerSidebarChrome(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome);\n"
            "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
            "    return xplorer_sidebar_chrome_;\n"
            "  }\n"
            "  // XPLORER: native \"Scheduled\" section, BELOW the tab list.\n"
            "  void InstallXplorerSidebarScheduled(\n"
            "      std::unique_ptr<xplorer::XplorerSidebarScheduledView> scheduled);\n"
            "  xplorer::XplorerSidebarScheduledView* xplorer_sidebar_scheduled() {\n"
            "    return xplorer_sidebar_scheduled_;\n"
            "  }",
        )
    # Scheduled-section member, appended AFTER the chrome view member's full
    # block. The guard checks the member-declaration text specifically (the
    # accessor above already put the bare name "xplorer_sidebar_scheduled_" into
    # the file). We RESTATE the chrome member block verbatim and add the
    # scheduled member after its last line, so the chrome member edit's exact
    # insertion stays present contiguously and its idempotency guard keeps
    # passing (splitting that block would make it re-fire and duplicate).
    if ("raw_ptr<xplorer::XplorerSidebarScheduledView> xplorer_sidebar_scheduled_"
            not in vts_h.read_text()):
        edit(
            vts_h,
            "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
            "      nullptr;  // XPLORER\n"
            "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;",
            "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
            "      nullptr;  // XPLORER\n"
            "  raw_ptr<VerticalTabStripTopContainer> top_button_container_ = nullptr;\n"
            "  raw_ptr<xplorer::XplorerSidebarScheduledView> xplorer_sidebar_scheduled_ =\n"
            "      nullptr;  // XPLORER",
        )

    vts_cc = src / "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc"
    edit(
        vts_cc,
        '#include "chrome/browser/ui/views/frame/browser_view.h"',
        '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"'
        "  // XPLORER",
    )
    if "xplorer_sidebar_scheduled_view.h" not in vts_cc.read_text():
        edit(
            vts_cc,
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"'
            "  // XPLORER",
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"'
            "  // XPLORER\n"
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_scheduled_view.h"'
            "  // XPLORER",
        )
    vts_cc_text = vts_cc.read_text()
    if "InstallXplorerSidebarChrome" not in vts_cc_text:
        edit(
            vts_cc,
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
            "void VerticalTabStripRegionView::InstallXplorerSidebarChrome(\n"
            "    std::unique_ptr<xplorer::XplorerSidebarChromeView> chrome) {\n"
            "  // Below the collapse/tab-search top bar, not above it.\n"
            "  size_t insert_index = 0;\n"
            "  if (top_button_separator_) {\n"
            "    insert_index = GetIndexOf(top_button_separator_).value() + 1;\n"
            "  } else if (top_button_container_) {\n"
            "    insert_index = GetIndexOf(top_button_container_).value() + 1;\n"
            "  }\n"
            "  xplorer_sidebar_chrome_ = AddChildViewAt(std::move(chrome), insert_index);\n"
            "  const int region_horizontal_padding =\n"
            "      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);\n"
            "  xplorer_sidebar_chrome_->SetProperty(\n"
            "      views::kMarginsKey, gfx::Insets::VH(0, region_horizontal_padding));\n"
            "  xplorer_sidebar_chrome_->SetProperty(\n"
            "      views::kFlexBehaviorKey,\n"
            "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
            "                               views::MaximumFlexSizeRule::kPreferred));\n"
            "}\n\n"
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
        )
    if "tab_strip_index" not in vts_cc.read_text():
        edit(
            vts_cc,
            "  std::optional<size_t> separator_index = GetIndexOf(top_button_separator_);\n"
            "  CHECK(separator_index.has_value());\n"
            "  ReorderChildView(tab_strip_view_, separator_index.value() + 1);",
            "  std::optional<size_t> separator_index = GetIndexOf(top_button_separator_);\n"
            "  CHECK(separator_index.has_value());\n"
            "  size_t tab_strip_index = separator_index.value() + 1;\n"
            "  if (xplorer_sidebar_chrome_) {\n"
            "    tab_strip_index = GetIndexOf(xplorer_sidebar_chrome_).value() + 1;\n"
            "  }\n"
            "  ReorderChildView(tab_strip_view_, tab_strip_index);  // XPLORER",
        )
    # InstallXplorerSidebarScheduled: append the "Scheduled" section below the tab
    # list. At install time tab_strip_view_ does not exist yet (the tab strip is
    # created lazily via InitializeTabStrip()/SetTabStripView, well after this
    # runs), so just append it for now; SetTabStripView re-anchors it directly
    # after tab_strip_view_ (see the reorder below). That ordering puts it between
    # the scrollable tab list and the bottom new-tab container.
    if "InstallXplorerSidebarScheduled" not in vts_cc.read_text():
        edit(
            vts_cc,
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
            "void VerticalTabStripRegionView::InstallXplorerSidebarScheduled(\n"
            "    std::unique_ptr<xplorer::XplorerSidebarScheduledView> scheduled) {\n"
            "  // Append for now; SetTabStripView() reorders it to sit just below\n"
            "  // tab_strip_view_ once the tab strip exists.\n"
            "  xplorer_sidebar_scheduled_ = AddChildView(std::move(scheduled));\n"
            "  const int region_horizontal_padding =\n"
            "      GetLayoutConstant(LayoutConstant::kVerticalTabStripHorizontalPadding);\n"
            "  xplorer_sidebar_scheduled_->SetProperty(\n"
            "      views::kMarginsKey, gfx::Insets::VH(0, region_horizontal_padding));\n"
            "  xplorer_sidebar_scheduled_->SetProperty(\n"
            "      views::kFlexBehaviorKey,\n"
            "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
            "                               views::MaximumFlexSizeRule::kPreferred));\n"
            "  if (tab_strip_view_) {\n"
            "    ReorderChildView(xplorer_sidebar_scheduled_,\n"
            "                     GetIndexOf(tab_strip_view_).value() + 1);\n"
            "  }\n"
            "}\n\n"
            "VerticalTabStripRegionView::~VerticalTabStripRegionView() {",
        )
    # Keep the Scheduled section directly below the tab list whenever the tab
    # strip view is (re)installed. Anchor on the SetTabStripView() tail (the
    # collapse-state call + return) so this is independent of whether the
    # tab_strip_index reorder line carries a "// XPLORER" suffix (it does on a
    # fresh apply, but may not on a tree patched by an earlier script).
    if "if (xplorer_sidebar_scheduled_) {  // XPLORER" not in vts_cc.read_text():
        edit(
            vts_cc,
            "  OnCollapseStateChanged(state_controller_->GetCollapseState());\n\n"
            "  return tab_strip_view_;",
            "  if (xplorer_sidebar_scheduled_) {  // XPLORER\n"
            "    ReorderChildView(xplorer_sidebar_scheduled_,\n"
            "                     GetIndexOf(tab_strip_view_).value() + 1);\n"
            "  }\n\n"
            "  OnCollapseStateChanged(state_controller_->GetCollapseState());\n\n"
            "  return tab_strip_view_;",
        )

    browser_view_h = src / "chrome/browser/ui/views/frame/browser_view.h"
    edit(
        browser_view_h,
        "class BookmarkBarView;",
        "class BookmarkBarView;\n"
        "namespace xplorer {\n"
        "class AgentTabGrouper;\n"
        "class XplorerSidebarChromeView;\n"
        "}  // namespace xplorer  // XPLORER",
    )
    edit(
        browser_view_h,
        "  TopContainerView* top_container() { return top_container_; }",
        "  TopContainerView* top_container() { return top_container_; }\n\n"
        "  // XPLORER: Arc-style vertical sidebar chrome accessors.\n"
        "  VerticalTabStripRegionView* vertical_tab_strip_region_view() {\n"
        "    return vertical_tab_strip_region_view_;\n"
        "  }\n"
        "  xplorer::XplorerSidebarChromeView* xplorer_sidebar_chrome() {\n"
        "    return xplorer_sidebar_chrome_;\n"
        "  }",
    )
    edit(
        browser_view_h,
        "  raw_ptr<BookmarkBarView> bookmark_bar_view_ = nullptr;",
        "  raw_ptr<BookmarkBarView> bookmark_bar_view_ = nullptr;\n"
        "  raw_ptr<xplorer::XplorerSidebarChromeView> xplorer_sidebar_chrome_ =\n"
        "      nullptr;  // XPLORER\n"
        "  std::unique_ptr<xplorer::AgentTabGrouper> agent_tab_grouper_;  // XPLORER",
    )

    browser_view_cc = src / "chrome/browser/ui/views/frame/browser_view.cc"
    edit(
        browser_view_cc,
        '#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"',
        '#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_agent_tab_grouper.h"  // XPLORER\n'
        '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"  // XPLORER',
    )
    # Append the scheduled-view include AFTER the chrome include block's last
    # line (restated), not inside it: the chrome include edit above has no outer
    # name-guard and relies on edit()'s verbatim-presence check, so splitting its
    # block would duplicate the whole block on the next apply.
    if "xplorer_sidebar_scheduled_view.h" not in browser_view_cc.read_text():
        edit(
            browser_view_cc,
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"  // XPLORER',
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_chrome_view.h"  // XPLORER\n'
            '#include "chrome/browser/ui/views/xplorer/xplorer_sidebar_scheduled_view.h"  // XPLORER',
        )
    edit(
        browser_view_cc,
        "    vertical_tab_strip_region_view_ =\n"
        "        AddChildView(std::move(vertical_tab_strip_container));",
        "    vertical_tab_strip_region_view_ =\n"
        "        AddChildView(std::move(vertical_tab_strip_container));\n\n"
        "    // XPLORER: Arc-style sidebar chrome (\"Tabs\" section label)\n"
        "    // + auto-group agent-owned tabs.\n"
        "    {\n"
        "      auto sidebar_chrome =\n"
        "          std::make_unique<xplorer::XplorerSidebarChromeView>(\n"
        "              browser_.get(), browser_->profile());\n"
        "      xplorer_sidebar_chrome_ = sidebar_chrome.get();\n"
        "      vertical_tab_strip_region_view_->InstallXplorerSidebarChrome(\n"
        "          std::move(sidebar_chrome));\n"
        "      agent_tab_grouper_ = std::make_unique<xplorer::AgentTabGrouper>(\n"
        "          browser_->GetTabStripModel());\n"
        "    }",
    )
    # XPLORER: native "Scheduled" section, installed AFTER the chrome view so it
    # renders below the tab list (sidebar order: Bookmarks -> Tabs -> Scheduled).
    # Spliced AFTER the chrome instantiation block's closing brace (restated
    # verbatim) rather than inside it: the chrome instantiation edit above has no
    # outer name-guard and relies on edit()'s verbatim-presence check, so
    # mutating its block would make it re-fire and duplicate on the next apply.
    # InstallXplorerSidebarScheduled only needs the region view member, so a
    # standalone block after the chrome block is fine.
    if "InstallXplorerSidebarScheduled" not in browser_view_cc.read_text():
        edit(
            browser_view_cc,
            "      agent_tab_grouper_ = std::make_unique<xplorer::AgentTabGrouper>(\n"
            "          browser_->GetTabStripModel());\n"
            "    }",
            "      agent_tab_grouper_ = std::make_unique<xplorer::AgentTabGrouper>(\n"
            "          browser_->GetTabStripModel());\n"
            "    }\n"
            "    // XPLORER: native \"Scheduled\" section below the tab list.\n"
            "    vertical_tab_strip_region_view_->InstallXplorerSidebarScheduled(\n"
            "        std::make_unique<xplorer::XplorerSidebarScheduledView>(\n"
            "            browser_.get()));",
        )

    browser_ui_gn = src / "chrome/browser/ui/BUILD.gn"
    # Order matters: the chrome-view block below adds the
    # xplorer_agent_tab_grouper.h line, which the scheduled_task_tabs block
    # anchors on. On a clean chromium reset that anchor does not exist until
    # this block adds it, so the chrome-view block MUST run first. Each block
    # keeps its own verbatim-presence guard for idempotency.
    observer_cc = '      "views/xplorer/xplorer_bookmark_tab_observer.cc",  # XPLORER\n'
    observer_h = '      "views/xplorer/xplorer_bookmark_tab_observer.h",  # XPLORER\n'
    gn_text = browser_ui_gn.read_text()
    if observer_cc in gn_text or observer_h in gn_text:
        gn_text = gn_text.replace(observer_cc, "").replace(observer_h, "")
        browser_ui_gn.write_text(gn_text)
        print(f"  removed bookmark_tab_observer from: {browser_ui_gn}")

    if "xplorer_sidebar_chrome_view.cc" not in browser_ui_gn.read_text():
        edit(
            browser_ui_gn,
            '      "views/bookmarks/bookmark_bar_view.cc",\n'
            '      "views/bookmarks/bookmark_bar_view.h",',
            '      "views/bookmarks/bookmark_bar_view.cc",\n'
            '      "views/bookmarks/bookmark_bar_view.h",\n'
            '      "views/xplorer/xplorer_sidebar_chrome_view.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_chrome_view.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_row_button.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_row_button.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_section_label.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_section_label.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_agent_tab_grouper.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_agent_tab_grouper.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_settings_nav.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_settings_nav.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_scheduled_view.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_sidebar_scheduled_view.h",  # XPLORER',
        )

    if "xplorer_scheduled_task_tabs.cc" not in browser_ui_gn.read_text():
        edit(
            browser_ui_gn,
            '      "views/xplorer/xplorer_agent_tab_grouper.h",  # XPLORER\n',
            '      "views/xplorer/xplorer_agent_tab_grouper.h",  # XPLORER\n'
            '      "views/xplorer/xplorer_scheduled_task_tabs.cc",  # XPLORER\n'
            '      "views/xplorer/xplorer_scheduled_task_tabs.h",  # XPLORER\n',
        )

    # XPLORER: Arc-style bookmark tabs hide their row from the vertical tab list.
    # Hiding is STATEFUL: a tab handle stays in `hidden_rows_` so it can be
    # re-asserted at the relayout boundary (OnAnimationEnded in the unpinned
    # container), mirroring how collapsed tab groups re-assert visibility. A
    # one-shot SetVisible(false) is otherwise clobbered when the insert
    # animation's TabCollectionAnimatingLayoutManager forces the new row back
    # visible for the duration of the animation.
    vts_view_h = (src / "chrome/browser/ui/views/tabs/vertical/"
                  "vertical_tab_strip_view.h")
    vts_view_h_text = vts_view_h.read_text()
    if "SetTabRowVisible" not in vts_view_h_text:
        # flat_set for the persistent set of hidden tab-row handles.
        edit(
            vts_view_h,
            '#include "base/memory/raw_ptr.h"',
            '#include "base/containers/flat_set.h"  // XPLORER\n'
            '#include "base/memory/raw_ptr.h"',
        )
        # Public API: SetTabRowVisible + ReassertHiddenRows.
        edit(
            vts_view_h,
            "  void OnTabChanged(const tabs::TabInterface* active_tab);\n\n"
            "  void RecordMousePressedInTab();",
            "  void OnTabChanged(const tabs::TabInterface* active_tab);\n\n"
            "  // XPLORER: hide/show a tab row (Arc-style sidebar bookmark tabs).\n"
            "  // Hiding is stateful so it survives insert + activate + the insert\n"
            "  // animation; ReassertHiddenRows() re-applies SetVisible(false) at\n"
            "  // the relayout boundary (the unpinned container's OnAnimationEnded).\n"
            "  void SetTabRowVisible(const tabs::TabHandle& handle, bool visible);\n"
            "  void ReassertHiddenRows();\n\n"
            "  void RecordMousePressedInTab();",
        )
        # Private member: the persistent set of hidden tab-row handles.
        edit(
            vts_view_h,
            "  bool is_collapsed_ = false;",
            "  bool is_collapsed_ = false;\n\n"
            "  // XPLORER: handles of tab rows hidden from the strip. Kept so the\n"
            "  // hidden state can be re-asserted after the insert animation, which\n"
            "  // would otherwise force a freshly-inserted row back to visible.\n"
            "  base::flat_set<tabs::TabHandle> hidden_rows_;",
        )
    vts_view_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
                   "vertical_tab_strip_view.cc")
    if "VerticalTabStripView::SetTabRowVisible" not in vts_view_cc.read_text():
        edit(
            vts_view_cc,
            "BEGIN_METADATA(VerticalTabStripView)",
            "void VerticalTabStripView::SetTabRowVisible(\n"
            "    const tabs::TabHandle& handle,\n"
            "    bool visible) {\n"
            "  // Track the hidden state so it can be re-asserted after the insert\n"
            "  // animation (see ReassertHiddenRows / the unpinned container's\n"
            "  // OnAnimationEnded).\n"
            "  if (visible) {\n"
            "    hidden_rows_.erase(handle);\n"
            "  } else {\n"
            "    hidden_rows_.insert(handle);\n"
            "  }\n"
            "  if (!collection_node_) {\n"
            "    return;\n"
            "  }\n"
            "  TabCollectionNode* node = collection_node_->GetNodeForHandle(handle);\n"
            "  if (!node || !node->view()) {\n"
            "    return;\n"
            "  }\n"
            "  node->view()->SetVisible(visible);\n"
            "  InvalidateLayout();\n"
            "}\n\n"
            "void VerticalTabStripView::ReassertHiddenRows() {\n"
            "  if (hidden_rows_.empty() || !collection_node_) {\n"
            "    return;\n"
            "  }\n"
            "  // Re-apply SetVisible(false) for every still-hidden row, dropping\n"
            "  // handles whose node/view has gone away. The next\n"
            "  // CalculateProposedLayout then reads GetVisible()==false and the row\n"
            "  // stays hidden.\n"
            "  for (auto it = hidden_rows_.begin(); it != hidden_rows_.end();) {\n"
            "    TabCollectionNode* node = collection_node_->GetNodeForHandle(*it);\n"
            "    if (!node || !node->view()) {\n"
            "      it = hidden_rows_.erase(it);\n"
            "      continue;\n"
            "    }\n"
            "    node->view()->SetVisible(false);\n"
            "    // XPLORER: a regroup reparents the row into a VerticalTabGroupView\n"
            "    // and leaves a stale opacity layer from the move-fade; undo it so\n"
            "    // the row isn't a transparent ghost when re-shown by a later layout.\n"
            "    if (node->view()->layer()) {\n"
            "      node->view()->layer()->SetOpacity(1.0f);\n"
            "    }\n"
            "    ++it;\n"
            "  }\n"
            "}\n\n"
            "BEGIN_METADATA(VerticalTabStripView)",
        )

    # XPLORER: re-assert hidden bookmark-tab rows at the relayout boundary.
    # The unpinned container is a TabCollectionAnimatingLayoutManager::Delegate;
    # overriding OnAnimationEnded() lets us re-apply SetVisible(false) right
    # after the insert animation ends (mirrors VerticalTabGroupView, which
    # re-asserts collapse there). Access the strip via the existing
    # GetVerticalTabStripView() ancestry helper — no new member, no xplorer dep.
    vutc_view_h = (src / "chrome/browser/ui/views/tabs/vertical/"
                   "vertical_unpinned_tab_container_view.h")
    if "OnAnimationEnded" not in vutc_view_h.read_text():
        edit(
            vutc_view_h,
            "  bool ShouldAnimateOpacityForAddAndRemove(\n"
            "      const views::View& child_view) const override;",
            "  bool ShouldAnimateOpacityForAddAndRemove(\n"
            "      const views::View& child_view) const override;\n"
            "  // XPLORER: re-assert hidden bookmark-tab rows once the insert/move\n"
            "  // animation settles, so the row stays hidden from the strip.\n"
            "  void OnAnimationEnded() override;",
        )
    vutc_view_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
                    "vertical_unpinned_tab_container_view.cc")
    vutc_cc_text = vutc_view_cc.read_text()
    if "VerticalUnpinnedTabContainerView::OnAnimationEnded" not in vutc_cc_text:
        edit(
            vutc_view_cc,
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"',
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"  // XPLORER\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"  // XPLORER',
        )
        edit(
            vutc_view_cc,
            "BEGIN_METADATA(VerticalUnpinnedTabContainerView)",
            "// XPLORER: re-apply the hidden state of bookmark-tab rows after the\n"
            "// animating layout manager finishes. The insert animation forces a\n"
            "// freshly-added row back to visible for its duration; re-asserting\n"
            "// here makes the hidden state durable (the next layout reads\n"
            "// GetVisible()==false and the row persists hidden).\n"
            "void VerticalUnpinnedTabContainerView::OnAnimationEnded() {\n"
            "  if (VerticalTabStripView* strip = GetVerticalTabStripView(this)) {\n"
            "    strip->ReassertHiddenRows();\n"
            "  }\n"
            "}\n\n"
            "BEGIN_METADATA(VerticalUnpinnedTabContainerView)",
        )

    # XPLORER: re-assert hidden rows at the GROUP-view relayout boundary too.
    # When the grouper regroups a hidden scheduled-task / bookmark row, the row is
    # reparented INTO a VerticalTabGroupView: DetachChildView force-shows it
    # (SetVisible(true)) and the reparent move-fade leaves a stale opacity layer.
    # The unpinned container's OnAnimationEnded never fires for that intra-group
    # move, so the row would otherwise stay half-visible with transparent title
    # text. Re-asserting from the group view's own OnAnimationEnded re-hides it.
    # The group-view .cc already includes vertical_tab_strip_utils.h (for
    # GetVerticalTabStripView) + vertical_tab_strip_view.h; add them only if a
    # future upstream drops them.
    vtg_view_h = (src / "chrome/browser/ui/views/tabs/vertical/"
                  "vertical_tab_group_view.h")
    if "void OnAnimationEnded() override;" not in vtg_view_h.read_text():
        sys.exit("ANCHOR NOT FOUND: VerticalTabGroupView::OnAnimationEnded "
                 "override decl missing — upstream moved; update apply_integration.py")
    vtg_view_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
                   "vertical_tab_group_view.cc")
    vtg_cc_text = vtg_view_cc.read_text()
    if ('#include "chrome/browser/ui/views/tabs/vertical/'
            'vertical_tab_strip_utils.h"') not in vtg_cc_text:
        edit(
            vtg_view_cc,
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"',
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_controller.h"\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"  // XPLORER',
        )
    if ('#include "chrome/browser/ui/views/tabs/vertical/'
            'vertical_tab_strip_view.h"') not in vtg_cc_text:
        edit(
            vtg_view_cc,
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"',
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_utils.h"\n'
            '#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_view.h"  // XPLORER',
        )
    if "strip->ReassertHiddenRows();" not in vtg_cc_text:
        edit(
            vtg_view_cc,
            "void VerticalTabGroupView::OnAnimationEnded() {\n"
            "  // For collapsed tab groups update child visibility only once animations have\n"
            "  // completed. This allows tabs to remain visible as the group animates closed.\n"
            "  if (tab_group_visual_data_.is_collapsed()) {\n"
            "    UpdateChildVisibilityForCollapseState(true);\n"
            "  }\n"
            "}",
            "void VerticalTabGroupView::OnAnimationEnded() {\n"
            "  // XPLORER: a scheduled-task / bookmark row reparented INTO this group is\n"
            "  // force-shown by DetachChildView and given an opacity layer by the reparent\n"
            "  // move-fade; re-assert its hidden state (the unpinned container's\n"
            "  // OnAnimationEnded never fires for an intra-group move).\n"
            "  if (VerticalTabStripView* strip = GetVerticalTabStripView(this)) {\n"
            "    strip->ReassertHiddenRows();\n"
            "  }\n"
            "  // For collapsed tab groups update child visibility only once animations have\n"
            "  // completed. This allows tabs to remain visible as the group animates closed.\n"
            "  if (tab_group_visual_data_.is_collapsed()) {\n"
            "    UpdateChildVisibilityForCollapseState(true);\n"
            "  }\n"
            "}",
        )

    # XPLORER: collapse the layout slot of a hidden tab row. CalculateProposedLayout
    # marks an invisible child as not-painted but still adds bounds.height() to the
    # running height, leaving a ~30px gap where an Arc bookmark tab's row was. Skip
    # invisible children entirely so the hidden row takes zero space.
    if "XPLORER: hidden rows take zero space" not in vutc_cc_text:
        edit(
            vutc_view_cc,
            "  for (auto* child : children) {\n"
            "    // The leading inset should not be applied for tab groups when the tab strip",
            "  for (auto* child : children) {\n"
            "    // XPLORER: hidden rows take zero space (Arc bookmark tabs are hidden\n"
            "    // from the strip; the sidebar bookmark row is the affordance).\n"
            "    if (!child->GetVisible()) {\n"
            "      layouts.child_layouts.emplace_back(child, false, gfx::Rect());\n"
            "      continue;\n"
            "    }\n"
            "    // The leading inset should not be applied for tab groups when the tab strip",
        )

    # XPLORER: "open chat" button on chat-owned tab-group headers. Each tab
    # group whose tabs are owned by a "chat:<conv_id>" agent gets a small button
    # on its header that opens the Grok side panel to that conversation. Mirrors
    # editor_bubble_button_'s pattern (a header button with a bound callback).
    # Patches PRISTINE upstream files; NO BUILD.gn dep is added (a grok_companion
    # dep here would create a GN cycle) — we rely on final-link symbol resolution,
    # the established fork pattern (cf. grok_native.cc including
    # grok_companion_util.h with no GN dep).
    vtgh_h = (src / "chrome/browser/ui/views/tabs/vertical/"
              "vertical_tab_group_header_view.h")
    # Forward-declare views::ImageButton alongside the other views fwd decls.
    edit(
        vtgh_h,
        "namespace views {\n"
        "class LabelButton;\n"
        "class ImageView;",
        "namespace views {\n"
        "class LabelButton;\n"
        "class ImageButton;  // XPLORER\n"
        "class ImageView;",
    )
    # New member: the open-chat button, next to editor_bubble_button_.
    edit(
        vtgh_h,
        "  const raw_ptr<views::LabelButton> editor_bubble_button_ = nullptr;",
        "  const raw_ptr<views::LabelButton> editor_bubble_button_ = nullptr;\n\n"
        "  // XPLORER: opens the Grok side panel to this group's chat\n"
        "  // conversation. Only shown for groups whose tabs are owned by a\n"
        '  // "chat:<conv_id>" agent (visibility toggled in OnDataChanged).\n'
        "  const raw_ptr<views::ImageButton> open_chat_button_ = nullptr;",
    )
    # New private method declaration.
    edit(
        vtgh_h,
        " private:\n"
        "  void UpdateEditorBubbleButtonVisibility();",
        " private:\n"
        "  // XPLORER: open the Grok side panel to this group's owning chat.\n"
        "  void OnOpenChatPressed();\n"
        "  void UpdateEditorBubbleButtonVisibility();",
    )

    vtgh_cc = (src / "chrome/browser/ui/views/tabs/vertical/"
               "vertical_tab_group_header_view.cc")
    # Includes (no BUILD.gn dep — final-link resolution, like grok_native.cc).
    edit(
        vtgh_cc,
        '#include "chrome/browser/ui/views/tabs/vertical/'
        'vertical_tab_group_header_view.h"',
        '#include "chrome/browser/ui/views/tabs/vertical/'
        'vertical_tab_group_header_view.h"\n'
        "\n"
        "// XPLORER: open-chat button — opens the Grok side panel to a\n"
        "// chat-owned tab group's conversation. These includes carry no GN dep\n"
        "// (final-link symbol resolution, the established fork pattern).\n"
        '#include "base/strings/string_util.h"  // XPLORER\n'
        '#include "chrome/browser/agent_gateway/tab_ownership.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
        '#include "chrome/browser/ui/views/frame/browser_view.h"  // XPLORER\n'
        '#include "components/tabs/public/tab_group.h"  // XPLORER\n'
        '#include "components/tabs/public/tab_interface.h"  // XPLORER\n'
        '#include "content/public/browser/web_contents.h"  // XPLORER\n'
        '#include "ui/views/controls/button/image_button.h"  // XPLORER',
    )
    # File-local helper: the owning chat conv_id (or "") for a group, read from
    # the first tab's TabOwnership. The group title is the chat TOPIC now, so we
    # must NOT parse the title — read ownership from the tab instead.
    edit(
        vtgh_cc,
        "class VerticalTabGroupHeaderLabel : public views::Label {",
        "// XPLORER: returns the conv_id of the chat agent that owns |group|'s\n"
        '// tabs, or "" if the group is not chat-owned (Bookmarks / Scheduled /\n'
        "// organize / non-chat agent groups). The group title is the human topic\n"
        "// now, so read ownership from the first tab's TabOwnership rather than\n"
        "// parsing the title.\n"
        "std::string GetOwningChatConvId(const TabGroup& group) {\n"
        "  if (tabs::TabInterface* first = group.GetFirstTab()) {\n"
        "    if (content::WebContents* wc = first->GetContents()) {\n"
        "      if (agent_gateway::TabOwnership* own =\n"
        "              agent_gateway::TabOwnership::Get(wc)) {\n"
        '        if (base::StartsWith(own->owner, "chat:")) {\n'
        '          return own->owner.substr(5);  // strip "chat:"\n'
        "        }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  return std::string();\n"
        "}\n"
        "\n"
        "class VerticalTabGroupHeaderLabel : public views::Label {",
    )
    # Constructor init-list: AddChildView the open-chat ImageButton next to the
    # editor button (mirrors editor_bubble_button_'s bound-callback init).
    edit(
        vtgh_cc,
        "      editor_bubble_button_(AddChildView(std::make_unique<views::LabelButton>(\n"
        "          base::BindRepeating(&VerticalTabGroupHeaderView::ShowEditorBubble,\n"
        "                              base::Unretained(this))))),\n"
        "      collapse_icon_(AddChildView(std::make_unique<views::ImageView>())),",
        "      editor_bubble_button_(AddChildView(std::make_unique<views::LabelButton>(\n"
        "          base::BindRepeating(&VerticalTabGroupHeaderView::ShowEditorBubble,\n"
        "                              base::Unretained(this))))),\n"
        "      open_chat_button_(AddChildView(std::make_unique<views::ImageButton>(\n"
        "          base::BindRepeating(&VerticalTabGroupHeaderView::OnOpenChatPressed,\n"
        "                              base::Unretained(this))))),  // XPLORER\n"
        "      collapse_icon_(AddChildView(std::make_unique<views::ImageView>())),",
    )
    # Constructor body: configure the open-chat button (hidden until OnDataChanged
    # finds a chat owner). Spliced right after ConfigureEditorBubbleButton().
    edit(
        vtgh_cc,
        "  ConfigureEditorBubbleButton(editor_bubble_button_);",
        "  ConfigureEditorBubbleButton(editor_bubble_button_);\n"
        "  // XPLORER: open-chat button — hidden unless this is a chat-owned group.\n"
        '  open_chat_button_->SetTooltipText(u"Open chat");\n'
        '  open_chat_button_->GetViewAccessibility().SetName(u"Open chat");\n'
        "  open_chat_button_->SetImageHorizontalAlignment(\n"
        "      views::ImageButton::ALIGN_CENTER);\n"
        "  open_chat_button_->SetImageVerticalAlignment(\n"
        "      views::ImageButton::ALIGN_MIDDLE);\n"
        "  open_chat_button_->SetVisible(false);\n"
        "  open_chat_button_->SetProperty(\n"
        "      views::kFlexBehaviorKey,\n"
        "      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,\n"
        "                               views::MaximumFlexSizeRule::kPreferred));",
    )
    # OnDataChanged: toggle visibility (chat-owned only) right after SetText.
    edit(
        vtgh_cc,
        "  group_header_label_->SetText(tab_group_visual_data_.title());",
        "  group_header_label_->SetText(tab_group_visual_data_.title());\n"
        "\n"
        "  // XPLORER: show the open-chat button only for chat-owned tab groups.\n"
        "  if (open_chat_button_) {\n"
        "    open_chat_button_->SetVisible(\n"
        "        !GetOwningChatConvId(delegate_->GetTabGroup()).empty());\n"
        "  }",
    )
    # OnDataChanged: tint the Grok icon like the header (inside the color block).
    edit(
        vtgh_cc,
        "    // Update editor bubble button.\n"
        "    UpdateEditorButtonColors(editor_bubble_button_, foreground_color);",
        "    // Update editor bubble button.\n"
        "    UpdateEditorButtonColors(editor_bubble_button_, foreground_color);\n"
        "\n"
        "    // XPLORER: tint the open-chat button's Grok icon to match the header.\n"
        "    open_chat_button_->SetImageModel(\n"
        "        views::Button::STATE_NORMAL,\n"
        "        ui::ImageModel::FromVectorIcon(kGrokIcon, foreground_color,\n"
        "                                       kIconSize));",
    )
    # OnOpenChatPressed: open the Grok side panel to the owning conversation.
    edit(
        vtgh_cc,
        "  editor_bubble_tracker_.Opened(delegate_->ShowGroupEditorBubble(\n"
        "      /*stop_context_menu_propagation=*/false));\n"
        "}\n"
        "\n"
        "BEGIN_METADATA(VerticalTabGroupHeaderView)",
        "  editor_bubble_tracker_.Opened(delegate_->ShowGroupEditorBubble(\n"
        "      /*stop_context_menu_propagation=*/false));\n"
        "}\n"
        "\n"
        "void VerticalTabGroupHeaderView::OnOpenChatPressed() {\n"
        "  // XPLORER: open the Grok side panel to this group's chat conversation.\n"
        "  const std::string conv = GetOwningChatConvId(delegate_->GetTabGroup());\n"
        "  if (conv.empty()) {\n"
        "    return;\n"
        "  }\n"
        "  views::Widget* widget = GetWidget();\n"
        "  if (!widget) {\n"
        "    return;\n"
        "  }\n"
        "  BrowserView* browser_view =\n"
        "      BrowserView::GetBrowserViewForNativeWindow(widget->GetNativeWindow());\n"
        "  if (browser_view && browser_view->browser()) {\n"
        "    grok_companion::OpenGrokSidePanelAt(browser_view->browser(),\n"
        '                                        "/?conv=" + conv);\n'
        "  }\n"
        "}\n"
        "\n"
        "BEGIN_METADATA(VerticalTabGroupHeaderView)",
    )


def main(src: Path):
    # 1. Start the AgentGateway once the browser UI is up.
    main_cc = src / "chrome/browser/chrome_browser_main.cc"
    # Must run after profile init (PostBrowserStart); earlier hooks crash on
    # unregistered prefs when the gateway resolves the profile dir.
    edit(
        main_cc,
        'TRACE_EVENT0("startup", "ChromeBrowserMainParts::PostBrowserStart");',
        f"\n  {MARKER}: start the AI-native agent gateway (HTTP 9334).\n"
        "  agent_gateway::AgentGateway::Start(0);\n",
    )
    edit(
        main_cc,
        '#include "chrome/browser/chrome_browser_main.h"',
        f'\n#include "chrome/browser/agent_gateway/agent_gateway.h"'
        f"  {MARKER}\n",
    )
    # 1b. Cleanly shut the gateway down in PostMainMessageLoopRun (after the main
    # loop quits, before thread teardown). The gateway is a leaked raw global
    # whose dtor never runs, so without this the AgentGateway server thread / its
    # net::HttpServer + listening socket / the Scheduler poll timer are never
    # torn down -> CompleteShutdown hangs and a zombie keeps port 9334 bound.
    # Anchor on the first Shutdown() in this method (UpgradeDetector), splicing
    # ours right after it — still well before browser_process_->StartTearDown().
    edit(
        main_cc,
        "  UpgradeDetector::GetInstance()->Shutdown();",
        f"\n\n  {MARKER}: deterministically stop the agent gateway server thread.\n"
        "  if (auto* g = agent_gateway::AgentGateway::GetInstance())\n"
        "    g->Shutdown();\n",
    )

    # 2. Link the component into chrome/browser.
    browser_gn = src / "chrome/browser/BUILD.gn"
    # Anchor on the unique warning comment inside static_library("browser")'s
    # public_deps block — the first bare `public_deps = [` in the file belongs
    # to a different target.
    edit(
        browser_gn,
        "public_deps = [\n    # WARNING WARNING WARNING",
        '\n    "//chrome/browser/agent_gateway",  # XPLORER',
    )

    # 3. Always start the CDP remote debugging server on 9333 — Xplorer treats
    # agents as first-class, no --remote-debugging-port flag needed. With no
    # switch present, default the port and let the existing policy-checked
    # startup path in GetInstance() do the rest.
    rds = src / "chrome/browser/devtools/remote_debugging_server.cc"
    edit(
        rds,
        "      command_line.GetSwitchValueASCII("
        "::switches::kRemoteDebuggingPort);",
        f"\n  {MARKER}: CDP always on for agents.\n"
        '  if (port_str.empty())\n    port_str = "9333";\n',
    )

    # 3b. AI-native runtime defaults: stop the browser from throttling or
    # suspending backgrounded/occluded tabs so an agent can drive many tabs at
    # full speed while Xplorer is inactive, and capture hidden windows. Injected
    # at the earliest startup hook (before FeatureList / GPU).
    cmd_delegate = src / "chrome/app/chrome_main_delegate.cc"
    edit(
        cmd_delegate,
        "std::optional<int> ChromeMainDelegate::BasicStartupComplete() {",
        f"\n  {MARKER}: AI-native defaults (never throttle backgrounded tabs) +\n"
        f"  {MARKER}: privacy — disable the component updater (Google pings), the\n"
        f"  {MARKER}: Finch field-trial config, and the Variations seed fetch\n"
        f"  {MARKER}: (empty server URLs). Brave/ungoogled-style: no phone-home.\n"
        "  {\n"
        "    base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();\n"
        '    for (const char* sw : {"disable-renderer-backgrounding",\n'
        '                           "disable-backgrounding-occluded-windows",\n'
        '                           "disable-background-timer-throttling",\n'
        '                           "disable-component-update",\n'
        '                           "disable-field-trial-config",\n'
        '                           "disable-domain-reliability",\n'
        '                           "disable-sync",\n'
        '                           "no-pings"}) {\n'
        "      if (!cmd->HasSwitch(sw))\n"
        "        cmd->AppendSwitch(sw);\n"
        "    }\n"
        '    for (const char* url_sw : {"variations-server-url",\n'
        '                               "variations-insecure-server-url"}) {\n'
        "      if (!cmd->HasSwitch(url_sw))\n"
        '        cmd->AppendSwitchASCII(url_sw, "");\n'
        "    }\n"
        '    if (!cmd->HasSwitch("disable-features"))\n'
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion,ChromeWhatsNewUI");\n'
        "  }\n",
    )

    # User-Agent / Sec-CH-UA: advertise the Xplorer brand alongside Chromium
    # (NEVER replace "Chromium"/"Google Chrome" or the Chrome/<ver> token —
    # site-compat). Appended in the shared brand-list builder so it shows in both
    # the low-entropy and full-version Sec-CH-UA brand lists. before=True with no
    # restated return line so edit() splices (the whitespace heuristic would
    # otherwise duplicate the return).
    ua_utils = src / "components/embedder_support/user_agent_utils.cc"
    edit(
        ua_utils,
        "  return ShuffleBrandList(brand_version_list, seed);",
        '  brand_version_list.emplace_back("Xplorer", version);  // XPLORER\n',
        before=True,
    )

    # 4. Branding: rename the product from "Chromium" to "Xplorer".
    branding = src / "chrome/app/theme/chromium/BRANDING"
    b = branding.read_text()
    if "PRODUCT_FULLNAME=Xplorer" not in b:
        for old in ("Chromium", "XBrowser"):
            b = b.replace(f"PRODUCT_FULLNAME={old}", "PRODUCT_FULLNAME=Xplorer")
            b = b.replace(f"PRODUCT_SHORTNAME={old}", "PRODUCT_SHORTNAME=Xplorer")
        b = b.replace("MAC_BUNDLE_ID=org.chromium.Chromium",
                      "MAC_BUNDLE_ID=org.xplorer.Xplorer")
        b = b.replace("MAC_BUNDLE_ID=org.xbrowser.XBrowser",
                      "MAC_BUNDLE_ID=org.xplorer.Xplorer")
        # Windows VERSIONINFO: CompanyName comes from these BRANDING keys (they
        # are baked into chrome.exe/chrome.dll via chrome_exe_version.rc.version).
        # Harmless on macOS, which keys off MAC_BUNDLE_ID / the product strings.
        b = b.replace("COMPANY_FULLNAME=The Chromium Authors",
                      "COMPANY_FULLNAME=Xplorer")
        b = b.replace("COMPANY_SHORTNAME=Chromium", "COMPANY_SHORTNAME=Xplorer")
        # Installer .exe file properties (setup.exe / mini_installer.exe
        # ProductName + FileDescription) — read "Chromium Installer" otherwise.
        b = b.replace("PRODUCT_INSTALLER_FULLNAME=Chromium Installer",
                      "PRODUCT_INSTALLER_FULLNAME=Xplorer Installer")
        b = b.replace("PRODUCT_INSTALLER_SHORTNAME=Chromium Installer",
                      "PRODUCT_INSTALLER_SHORTNAME=Xplorer Installer")
        branding.write_text(b)
        print(f"  edited: {branding}")

    # app-Info.plist hardcodes two UTTypeDescription strings as "Chromium
    # Extension"/"Chromium Shortcut" (all the other fields use the substituted
    # ${CHROMIUM_SHORT_NAME}, which resolves to "Xplorer"). Route these through
    # the same variable so Finder's Get-Info on .crx/app-shortcut files reads
    # "Xplorer …" instead of "Chromium …".
    app_info = src / "chrome/app/app-Info.plist"
    ai = app_info.read_text()
    if "${CHROMIUM_SHORT_NAME} Extension" not in ai:
        ai = ai.replace("<string>Chromium Extension</string>",
                        "<string>${CHROMIUM_SHORT_NAME} Extension</string>")
        ai = ai.replace("<string>Chromium Shortcut</string>",
                        "<string>${CHROMIUM_SHORT_NAME} Shortcut</string>")
        app_info.write_text(ai)
        print(f"  edited: {app_info}")

    # XPLORER: Sparkle 2.x auto-update keys. tweak_info_plist passes unknown
    # keys through to the built Info.plist unchanged, so writing them into the
    # template here is sufficient. SUFeedURL is the appcast (GitHub Pages),
    # SUPublicEDKey is the EdDSA public key the updater verifies signatures
    # against, and the AutomaticChecks/ScheduledCheckInterval pair makes Sparkle
    # self-check daily. NOTE: deliberately no SUEnableInstallerLauncherService —
    # the app is not sandboxed, so the launcher XPC service must NOT be enabled.
    ai = app_info.read_text()
    if "SUFeedURL" not in ai:
        su_keys = (
            "\t<key>SUFeedURL</key>\n"
            "\t<string>https://daniel-farina.github.io/xplorer/appcast.xml</string>\n"
            "\t<key>SUPublicEDKey</key>\n"
            "\t<string>1dT5/+AbAMKH6F1IrtejPfrplH9JVKDqMLGfhzQhaiI=</string>\n"
            "\t<key>SUEnableAutomaticChecks</key>\n"
            "\t<true/>\n"
            "\t<key>SUScheduledCheckInterval</key>\n"
            "\t<integer>86400</integer>\n"
        )
        ai = ai.replace("</dict>\n</plist>", su_keys + "</dict>\n</plist>")
        app_info.write_text(ai)
        print(f"  added Sparkle keys: {app_info}")

    # XPLORER: wire the Sparkle auto-updater into browser startup. Add the
    # include next to app_controller_mac.h, and kick off the updater right after
    # the controller marks startup complete in -applicationDidFinishLaunching:.
    app_controller = src / "chrome/browser/app_controller_mac.mm"
    t = app_controller.read_text()
    if "XplorerStartSparkleUpdater" not in t:
        t = t.replace(
            '#import "chrome/browser/app_controller_mac.h"\n',
            '#import "chrome/browser/app_controller_mac.h"\n'
            '#import "chrome/browser/xplorer_sparkle_updater.h"\n',
        )
        t = t.replace(
            "  _startupComplete = YES;\n",
            "  _startupComplete = YES;\n"
            "  XplorerStartSparkleUpdater();  // XPLORER: Sparkle auto-update\n",
        )
        app_controller.write_text(t)
        print(f"  edited: {app_controller}")

    # XPLORER: compile the Sparkle updater glue into static_library("browser").
    # Append next to app_controller_mac.mm in the is_mac sources block (paths in
    # that block are relative to chrome/browser, so bare filenames). Sparkle is
    # runtime-loaded, so there is no GN framework dep / rpath to add.
    if "xplorer_sparkle_updater.mm" not in browser_gn.read_text():
        edit(
            browser_gn,
            '      "app_controller_mac.mm",\n',
            '      "app_controller_mac.mm",\n'
            '      "xplorer_sparkle_updater.h",  # XPLORER\n'
            '      "xplorer_sparkle_updater.mm",  # XPLORER\n',
        )

    # The visible app name comes from IDS_PRODUCT_NAME / IDS_SHORT_PRODUCT_NAME
    # in the (non-Google, non-CfT) else branch of chromium_strings.grd.
    grd = src / "chrome/app/chromium_strings.grd"
    g = grd.read_text()
    if ">\n            Xplorer\n" not in g:
        for old in ("Chromium", "XBrowser"):
            g = g.replace(
                'desc="The Chrome application name" translateable="false">\n'
                f"            {old}\n",
                'desc="The Chrome application name" translateable="false">\n'
                "            Xplorer\n",
            )
            g = g.replace(
                'desc="The Chrome application short name." translateable="false">\n'
                f"            {old}\n",
                'desc="The Chrome application short name." translateable="false">\n'
                "            Xplorer\n",
            )
        grd.write_text(g)
        print(f"  edited: {grd}")

    # The window/tab/accessible title formats hardcode "- Chromium" (they do NOT
    # use the renamed product placeholder), so titles read "<page> - Chromium".
    # Rebrand the title-format messages: covers the stable browser title, the
    # macOS accessible title + channel variants (Beta/Dev/Canary), and the
    # ChromeOS / captive-portal layouts.
    g = grd.read_text()
    if "</ph> - Xplorer" not in g:
        g = g.replace("</ph> - Chromium", "</ph> - Xplorer")
        g = g.replace("Chromium - <ph", "Xplorer - <ph")
        g = g.replace("- Network Sign-in - Chromium", "- Network Sign-in - Xplorer")
        g = g.replace("Chromium - Network Sign-in", "Xplorer - Network Sign-in")
        grd.write_text(g)
        print(f"  edited (title formats): {grd}")

    # Broad app-name rebrand: ~700 user-facing strings in chromium_strings.grd
    # still hardcode "Chromium" (default-browser prompt, profile/startup errors,
    # background-run, update nags, etc.) — the product-name rename only covered
    # IDS_PRODUCT_NAME. Replace them all with Xplorer, preserving the legal
    # "Chromium Authors" copyright. Risk-checked: no <message name="…"> IDs
    # contain "Chromium", so this only touches text content + translator descs,
    # never resource IDs. Guard: post-rebrand only the copyright keeps "Chromium".
    g = grd.read_text()
    if g.count("Chromium") > g.count("Chromium Authors"):
        g = g.replace("Chromium Authors", "\x00AUTH\x00")
        g = g.replace("Chromium", "Xplorer")
        g = g.replace("\x00AUTH\x00", "Chromium Authors")
        grd.write_text(g)
        print(f"  edited (broad app-name rebrand): {grd}")

    # Extend the same safe rebrand to every other user-facing strings file —
    # settings (148), omnibox pedals (40), components, search-engine choice,
    # privacy sandbox, password manager, page info, autofill, … ~300 more
    # hardcoded "Chromium" app-name refs. Glob *strings.grd/.grdp only (skips
    # resource/image grds); generated_resources.grd uses PRODUCT_NAME subst so
    # only its few literals need touching.
    for base, pat in (("chrome/app", "*strings.grd"), ("chrome/app", "*strings.grdp"),
                      ("components", "**/*strings.grd"), ("components", "**/*strings.grdp")):
        for f in sorted((src / base).glob(pat)):
            rebrand_grd_strings(f)
    rebrand_grd_strings(src / "chrome/app/generated_resources.grd")

    # 5. Link Grok companion (side panel + AI Mode redirect).
    edit(
        browser_gn,
        '\n    "//chrome/browser/agent_gateway",  # XPLORER',
        '\n    "//chrome/browser/grok_companion",  # XPLORER',
    )

    # AI Mode omnibox chip -> open native Grok Search page (not Google AI Mode).
    ai_mode_icon = src / "chrome/browser/ui/views/location_bar/ai_mode_page_action_icon_view.cc"
    edit(
        ai_mode_icon,
        'void AiModePageActionIconView::OnExecuting(\n'
        '    PageActionIconView::ExecuteSource source) {\n'
        '  OmniboxController* omnibox_controller =\n'
        '      search::GetOmniboxController(GetWebContents());\n'
        '  CHECK(omnibox_controller);\n'
        '  omnibox::AiModePageActionController::OpenAiMode(*omnibox_controller,\n'
        '                                                  /*via_keyboard=*/false);\n'
        '}',
        'void AiModePageActionIconView::OnExecuting(\n'
        '    PageActionIconView::ExecuteSource source) {\n'
        '  // XPLORER: Grok chip opens native Grok Search.\n'
        '  grok_companion::OpenGrokSearchPage(browser_);\n'
        '}',
    )
    edit(
        ai_mode_icon,
        '    omnibox::AiModePageActionController::OpenAiMode(*omnibox_controller,\n'
        '                                                    /*via_keyboard=*/true);\n'
        '    return true;',
        '    grok_companion::OpenGrokSearchPage(browser_);\n'
        '    return true;',
    )
    edit(
        ai_mode_icon,
        '  SetUseTonalColorsWhenExpanded(true);\n'
        '  SetBackgroundVisibility(BackgroundVisibility::kWithLabel);\n'
        '}',
        '  SetUseTonalColorsWhenExpanded(true);\n'
        '  SetBackgroundVisibility(BackgroundVisibility::kWithLabel);\n'
        '  SetTooltipText(u"Open Grok Search");\n'
        '}',
    )
    edit(
        ai_mode_icon,
        '#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n',
    )
    # Redirect OpenAiMode (command callback) to Grok Search.
    ai_mode_ctrl = src / "chrome/browser/ui/omnibox/ai_mode_page_action_controller.cc"
    edit(
        ai_mode_ctrl,
        'void AiModePageActionController::OpenAiMode(\n'
        '    OmniboxController& omnibox_controller,\n'
        '    bool via_keyboard) {\n'
        '  omnibox_controller.edit_model()->OpenAiMode(via_keyboard,\n'
        '                                              /*via_context_menu=*/false);\n'
        '}',
        'void AiModePageActionController::OpenAiMode(\n'
        '    OmniboxController& omnibox_controller,\n'
        '    bool via_keyboard) {\n'
        '  // XPLORER: never open Google AI Mode — use native Grok Search.\n'
        '  OmniboxClient* client = omnibox_controller.client();\n'
        '  if (!client || !client->IsChromeOmniboxClient()) {\n'
        '    return;\n'
        '  }\n'
        '  Browser* browser = static_cast<ChromeOmniboxClient*>(client)->browser();\n'
        '  if (browser) {\n'
        '    grok_companion::OpenGrokSearchPage(browser);\n'
        '  }\n'
        '}',
    )
    edit(
        ai_mode_ctrl,
        '#include "chrome/browser/ui/omnibox/ai_mode_page_action_controller.h"',
        '#include "chrome/browser/ui/omnibox/ai_mode_page_action_controller.h"\n'
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
        '#include "chrome/browser/ui/browser.h"  // XPLORER\n'
        '#include "chrome/browser/ui/omnibox/chrome_omnibox_client.h"  // XPLORER\n',
    )
    edit(
        ai_mode_ctrl,
        'bool AiModePageActionController::ShouldShowPageAction(\n'
        '    Profile* profile,\n'
        '    LocationBarView& location_bar_view) {',
        'bool AiModePageActionController::ShouldShowPageAction(\n'
        '    Profile* profile,\n'
        '    LocationBarView& location_bar_view) {\n'
        '  // XPLORER: always show Grok entrypoint in Xplorer.\n'
        '  if (profile && profile->IsRegularProfile()) {\n'
        '    return true;\n'
        '  }',
    )

    # Register grok.com toolbar overlay early (before side panel / NTP race).
    browser_features = (
        src / "chrome/browser/ui/browser_window/internal/browser_window_features.cc")
    edit(
        browser_features,
        '#include "chrome/browser/ui/views/side_panel/side_panel_coordinator.h"\n',
        '#include "chrome/browser/grok_companion/grok_fab.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_web_bar.h"  // XPLORER\n',
        before=True,
    )
    edit(
        browser_features,
        '  // TODO(crbug.com/346148093): Move SidePanelCoordinator construction to\n'
        '  // Init.',
        '  // XPLORER: grok.com/grokipedia toolbar before side panel init (NTP race).\n'
        '  grok_companion::RegisterGrokWebBar(browser);\n'
        '  grok_companion::RegisterGrokFab(browser);\n\n'
        '  // TODO(crbug.com/346148093): Move SidePanelCoordinator construction to\n'
        '  // Init.',
    )

    # Register Grok side panel in global entries.
    side_panel_helper = (
        src / "chrome/browser/ui/views/side_panel/side_panel_helper.cc")
    edit(
        side_panel_helper,
        '#include "chrome/browser/ui/views/side_panel/reading_list/reading_list_side_panel_coordinator.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_fab.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_web_bar.h"  // XPLORER\n',
    )
    edit(
        side_panel_helper,
        '  // Add bookmarks.\n'
        '  BookmarksSidePanelCoordinator::From(browser)->CreateAndRegisterEntry(\n'
        '      window_registry);',
        '  // Add bookmarks.\n'
        '  BookmarksSidePanelCoordinator::From(browser)->CreateAndRegisterEntry(\n'
        '      window_registry);\n\n'
        '  // XPLORER: Grok AI companion side panel + grok.com toolbar overlay.\n'
        '  grok_companion::RegisterGrokWebBar(browser);\n'
        '  grok_companion::RegisterGrokFab(browser);\n'
        '  grok_companion::RegisterGrokSidePanel(browser);',
    )

    # New tab page -> Grok search homepage is handled by Browser::GetNewTabURL
    # (below) + grok_companion's legacy-NTP redirect. We deliberately do NOT
    # patch search.cc's GetNewTabPageURL: an unconditional early return there
    # makes the rest of the function unreachable, which fails -Werror.

    # New tabs must navigate to the Grok home URL directly — not chrome://newtab
    # (injectors only run on http/https pages).
    browser_cc = src / "chrome/browser/ui/browser.cc"
    edit(
        browser_cc,
        '#include "chrome/browser/ui/browser.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n',
    )
    edit(
        browser_cc,
        'GURL Browser::GetNewTabURL() const {\n'
        '  if (auto* const app_browser_controller =\n'
        '          web_app::AppBrowserController::From(this)) {\n'
        '    return app_browser_controller->GetAppNewTabUrl();\n'
        '  }\n'
        '  return chrome::ChromeUINewTabURLAsGURL();\n}',
        'GURL Browser::GetNewTabURL() const {\n'
        '  if (auto* const app_browser_controller =\n'
        '          web_app::AppBrowserController::From(this)) {\n'
        '    return app_browser_controller->GetAppNewTabUrl();\n'
        '  }\n'
        '  // XPLORER: open Grok home directly so page injectors can attach.\n'
        '  return grok_companion::GetStartupHomeURL();\n}',
    )

    tab_restore_client = (
        src / "chrome/browser/sessions/chrome_tab_restore_service_client.cc")
    edit(
        tab_restore_client,
        '#include "chrome/browser/sessions/chrome_tab_restore_service_client.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n',
    )
    edit(
        tab_restore_client,
        'GURL ChromeTabRestoreServiceClient::GetNewTabURL() {\n'
        '  return chrome::ChromeUINewTabURLAsGURL();\n}',
        'GURL ChromeTabRestoreServiceClient::GetNewTabURL() {\n'
        '  // XPLORER: match Browser::GetNewTabURL().\n'
        '  return grok_companion::GetStartupHomeURL();\n}',
    )

    # New tabs land on the Grok home (an http gateway page). Render it with a
    # blank omnibox like the NTP instead of exposing the internal gateway URL.
    loc_delegate = (
        src / "chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.cc")
    edit(
        loc_delegate,
        '#include "components/search/ntp_features.h"',
        '#include "components/search/ntp_features.h"\n'
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER',
    )
    edit(
        loc_delegate,
        '  GURL url = entry->GetURL();\n'
        '  if (is_ntp(entry->GetVirtualURL()) || is_ntp(url)) {\n'
        '    return false;\n'
        '  }',
        '  GURL url = entry->GetURL();\n'
        '  if (is_ntp(entry->GetVirtualURL()) || is_ntp(url)) {\n'
        '    return false;\n'
        '  }\n'
        '\n'
        '  // XPLORER: our own gateway home pages (the new-tab home) get a blank\n'
        '  // omnibox, just like the NTP.\n'
        '  if (grok_companion::IsGrokHomeURL(url) ||\n'
        '      grok_companion::IsGrokHomeURL(entry->GetVirtualURL())) {\n'
        '    return false;\n'
        '  }',
    )

    # Enable AI Mode omnibox entrypoint feature flag.
    edit(
        cmd_delegate,
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion,ChromeWhatsNewUI");\n'
        '  }\n',
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion,ChromeWhatsNewUI");\n'
        '    if (!cmd->HasSwitch("enable-features"))\n'
        '      cmd->AppendSwitchASCII("enable-features",\n'
        '                             "AiModeOmniboxEntryPoint");\n'
        '    if (!cmd->HasSwitch("top-chrome-touch-ui"))\n'
        '      cmd->AppendSwitchASCII("top-chrome-touch-ui", "enabled");\n'
        '  }\n',
    )

    # XPLORER: vertical tabs (tabs to the side) on by default — enable the
    # feature + expand-on-hover, and flip the layout pref so a fresh profile
    # opens with the side tab strip rather than the horizontal strip.
    tabs_features = src / "chrome/browser/ui/tabs/features.cc"
    # NOTE: edit() only *replaces* the anchor when the insertion restates its
    # first or last line; a bare one-line value flip is treated as additive and
    # gets spliced (duplicated). So anchor each flip together with an unchanged
    # neighbouring line.
    edit(
        tabs_features,
        "BASE_FEATURE(kVerticalTabs, base::FEATURE_DISABLED_BY_DEFAULT);\n\n"
        "BASE_FEATURE(kVerticalTabsLaunch, base::FEATURE_DISABLED_BY_DEFAULT);",
        "BASE_FEATURE(kVerticalTabs, base::FEATURE_ENABLED_BY_DEFAULT);\n\n"
        "BASE_FEATURE(kVerticalTabsLaunch, base::FEATURE_DISABLED_BY_DEFAULT);",
    )
    edit(
        tabs_features,
        "BASE_FEATURE(kVerticalTabsExpandOnHover, "
        "base::FEATURE_DISABLED_BY_DEFAULT);\nBASE_FEATURE_PARAM(bool,",
        "BASE_FEATURE(kVerticalTabsExpandOnHover, "
        "base::FEATURE_ENABLED_BY_DEFAULT);\nBASE_FEATURE_PARAM(bool,",
    )
    tab_strip_prefs = src / "chrome/browser/ui/tabs/tab_strip_prefs.cc"
    edit(
        tab_strip_prefs,
        "  registry->RegisterBooleanPref(prefs::kEverythingMenuPinnedToTabstrip,"
        " true);\n"
        "  registry->RegisterBooleanPref(prefs::kVerticalTabsEnabled, false);",
        "  registry->RegisterBooleanPref(prefs::kEverythingMenuPinnedToTabstrip,"
        " true);\n"
        "  registry->RegisterBooleanPref(prefs::kVerticalTabsEnabled, true);",
    )

    # Rename "AI Mode" label to "Grok" in the omnibox chip.
    g = grd.read_text()
    if "IDS_AI_MODE_ENTRYPOINT_LABEL" in g and ">Grok<" not in g:
        g = g.replace(
            '<message name="IDS_AI_MODE_ENTRYPOINT_LABEL"\n'
            '        desc="The label of the AI mode entrypoint in the omnibox" formatter_data="android_java">\n'
            '        AI Mode\n'
            '      </message>',
            '<message name="IDS_AI_MODE_ENTRYPOINT_LABEL"\n'
            '        desc="The label of the AI mode entrypoint in the omnibox" formatter_data="android_java">\n'
            '        Grok\n'
            '      </message>',
        )
        grd.write_text(g)
        print(f"  edited: {grd}")

    # Attach Grok page injectors to every tab at creation time.
    tab_helpers = src / "chrome/browser/ui/tab_helpers.cc"
    edit(
        tab_helpers,
        '#include "chrome/browser/ui/tab_helpers.h"',
        '\n#include "chrome/browser/grok_companion/grok_fab.h"  // XPLORER\n'
        '#include "chrome/browser/grok_companion/grok_web_bar.h"  // XPLORER\n',
    )
    edit(
        tab_helpers,
        '  // --- Section 1: Common tab helpers ---',
        '  // XPLORER: Grok toolbar + floating page button on every regular tab.\n'
        '  if (profile && profile->IsRegularProfile()) {\n'
        '    grok_companion::AttachGrokWebBarInjector(web_contents);\n'
        '    grok_companion::AttachGrokFabInjector(web_contents);\n'
        '  }\n\n'
        '  // --- Section 1: Common tab helpers ---',
    )

    # Toolbar Grok button (top-right icon that toggles the Grok agent side
    # panel — the native sidebar where you chat with Grok and it drives the
    # browser via MCP). Grok logo icon.
    toolbar = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    grok_btn_block = (
        '  // XPLORER: Grok companion toolbar button.\n'
        '  {\n'
        '    auto grok_btn = std::make_unique<ToolbarButton>(base::BindRepeating(\n'
        '        [](Browser* b) {\n'
        '          if (b)\n'
        '            grok_companion::ToggleGrokSidePanel(b);\n'
        '        },\n'
        '        base::Unretained(browser_)));\n'
        '    grok_btn->SetTooltipText(u"Ask Grok");\n'
        '    grok_btn->SetAccessibleName(u"Ask Grok");\n'
        '    grok_btn->SetVectorIcon(kGrokIcon);\n'
        '    grok_btn->SetProperty(views::kElementIdentifierKey,\n'
        '                          kToolbarGrokButtonElementId);\n'
        '    AddChildView(std::move(grok_btn));\n'
        '  }\n\n'
    )
    if False:  # XPLORER: explicit Grok button dropped; the default-pinned
        # kSearchCompanion side-panel button is the single always-visible toggle.
        if "ToggleGrokSidePanel" in toolbar.read_text():
            edit(
                toolbar,
                '  // XPLORER: Grok companion toolbar button.\n'
                '  {\n'
                '    auto grok_btn = std::make_unique<ToolbarButton>(base::BindRepeating(\n'
                '        [](Browser* b) {\n'
                '          if (b)\n'
                '            grok_companion::ToggleGrokSidePanel(b);\n'
                '        },\n'
                '        base::Unretained(browser_)));\n'
                '    grok_btn->SetTooltipText(u"Grok");\n'
                '    grok_btn->SetAccessibleName(u"Grok");\n'
                '    grok_btn->SetVectorIcon(vector_icons::kLightbulbIcon);\n'
                '    grok_btn->SetProperty(views::kElementIdentifierKey,\n'
                '                          kToolbarGrokButtonElementId);\n'
                '    AddChildView(std::move(grok_btn));\n'
                '  }\n\n',
                grok_btn_block,
            )
            edit(
                toolbar,
                '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
                '#include "components/vector_icons/vector_icons.h"\n'
                '#include "chrome/browser/ui/views/toolbar/toolbar_button.h"\n',
                '#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
                '#include "chrome/app/vector_icons/vector_icons.h"  // XPLORER\n'
                '#include "chrome/browser/ui/views/toolbar/toolbar_button.h"\n',
            )
        elif "GetGrokToolbarIcon" in toolbar.read_text():
            edit(
                toolbar,
                '    grok_btn->SetImageModel(views::Button::STATE_NORMAL,\n'
                '                            grok_companion::GetGrokToolbarIcon());\n'
                '    grok_btn->SetImageModel(views::Button::STATE_HOVERED,\n'
                '                            grok_companion::GetGrokToolbarIcon());\n'
                '    grok_btn->SetImageModel(views::Button::STATE_PRESSED,\n'
                '                            grok_companion::GetGrokToolbarIcon());\n',
                '    grok_btn->SetVectorIcon(kGrokIcon);\n',
            )
            edit(
                toolbar,
                '#include "chrome/browser/grok_companion/grok_toolbar_icon.h"  // XPLORER\n',
                '#include "chrome/app/vector_icons/vector_icons.h"  // XPLORER\n',
            )
        else:
            # Splice the Grok button BEFORE the overflow button — purely additive.
            # Do NOT restate the overflow_button_ line in the insertion: edit()'s
            # "rewrite vs splice" heuristic compares anchor.strip() (de-indented)
            # against the insertion's still-indented lines, so the last-line match
            # misses and it splices additively. Restating the line then leaves a
            # SECOND, visible, controller-less OverflowButton whose RunMenu() calls
            # ToolbarController::ShowMenu() on a null controller → crash on click.
            edit(
                toolbar,
                '  overflow_button_ = AddChildView(std::make_unique<OverflowButton>());',
                grok_btn_block,
                before=True,
            )
            edit(
                toolbar,
                '#include "chrome/browser/ui/views/toolbar/toolbar_view.h"',
                '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // XPLORER\n'
                '#include "chrome/app/vector_icons/vector_icons.h"  // XPLORER\n'
                '#include "chrome/browser/ui/views/toolbar/toolbar_button.h"\n',
            )
    # Grok logo vector icon for toolbar button.
    vector_icons_gn = src / "chrome/app/vector_icons/BUILD.gn"
    edit(
        vector_icons_gn,
        '    "grid_view.icon",\n',
        '    "grid_view.icon",\n    "grok.icon",\n',
    )

    # Element id for the Grok toolbar button (local to toolbar_view.cc).
    browser_elements = src / "chrome/browser/ui/browser_element_identifiers.h"
    edit(
        browser_elements,
        'DECLARE_ELEMENT_IDENTIFIER_VALUE(kLocationBarElementId);',
        'DECLARE_ELEMENT_IDENTIFIER_VALUE(kToolbarGrokButtonElementId);\n',
    )
    browser_elements_cc = src / "chrome/browser/ui/browser_element_identifiers.cc"
    edit(
        browser_elements_cc,
        'DEFINE_ELEMENT_IDENTIFIER_VALUE(kLocationBarElementId);',
        'DEFINE_ELEMENT_IDENTIFIER_VALUE(kToolbarGrokButtonElementId);\n',
    )

    # Register an actions::ActionItem for the Grok companion side panel. The
    # native side-panel header controller calls GetActionItem(entry->key()) and
    # unconditionally AddActionChangedCallback() on it. For the reused
    # kSearchCompanion id Chrome only registers that ActionItem when the (absent)
    # companion feature is enabled, so without this the header derefs null and the
    # browser SIGSEGVs the first time the Grok side panel opens (the "Ask Grok"
    # toolbar button). Reuse the Grok icon + the branded "Grok" label.
    browser_actions = src / "chrome/browser/ui/browser_actions.cc"
    edit(
        browser_actions,
        "  BrowserWindowInterface* const bwi = base::to_address(bwi_);\n",
        "  BrowserWindowInterface* const bwi = base::to_address(bwi_);\n\n"
        "  // XPLORER: register the Grok companion side-panel action so the\n"
        "  // side-panel header controller finds a non-null ActionItem.\n"
        "  root_action_item_->AddChild(\n"
        "      SidePanelAction(SidePanelEntryId::kSearchCompanion,\n"
        "                      IDS_AI_MODE_ENTRYPOINT_LABEL,\n"
        "                      IDS_AI_MODE_ENTRYPOINT_LABEL, kGrokIcon,\n"
        "                      kActionSidePanelShowSearchCompanion, bwi, true)\n"
        "          .Build());\n",
    )

    # XPLORER: pin the Grok side-panel button by default so it is the single,
    # always-visible Grok toggle — shown in BOTH the inactive (panel closed) and
    # active (panel open, highlighted) states. The native side-panel button is
    # only shown ephemerally when active, so without this the button vanishes
    # when the panel is closed. Inserted before the CanUpdate gate so it applies
    # regardless of toolbar customization; UpdatePinnedState is idempotent.
    pinned_model = src / ("chrome/browser/ui/toolbar/pinned_toolbar/"
                          "pinned_toolbar_actions_model.cc")
    edit(
        pinned_model,
        "void PinnedToolbarActionsModel::MaybeMigrateExistingPinnedStates() {\n"
        "  if (!CanUpdate()) {\n"
        "    return;\n"
        "  }\n",
        "void PinnedToolbarActionsModel::MaybeMigrateExistingPinnedStates() {\n"
        "  // XPLORER: keep the Grok side-panel button pinned (always visible).\n"
        "  UpdatePinnedState(kActionSidePanelShowSearchCompanion, true);\n"
        "  if (!CanUpdate()) {\n"
        "    return;\n"
        "  }\n",
    )

    # --- Grok as the default search engine --------------------------------
    # Repoint the prepopulated "google" fallback entry (the first-run default,
    # keyed on id==1) at the gateway GET /omnibox 302 handoff, so typing a
    # query in the address bar searches Grok. The engine short_name "Grok"
    # also makes the omnibox placeholder read "Search Grok or type a URL".
    # suggest_url is emptied to drop Google autocomplete suggestions, and
    # kCurrentDataVersion is bumped so existing profiles re-merge the change.
    prepop = src / ("third_party/search_engines_data/resources/definitions/"
                    "prepopulated_engines.json")
    pp = prepop.read_text()
    if "127.0.0.1:9334/omnibox" not in pp:
        # name + keyword: whitespace-tolerant regex with a hard assert. The old
        # literal .replace() silently no-ops on any format drift (unlike edit()),
        # which would ship Google as the default search engine. Fail LOUD here so
        # an upstream format change is caught at patch time, not in the binary.
        pp, n = re.subn(
            r'"name":\s*"Google",\s*"keyword":\s*"google\.com",',
            '"name": "Grok",\n      "keyword": "grok.com",', pp)
        if n != 1:
            sys.exit("prepopulated_engines.json: Google name/keyword anchor "
                     f"matched {n}x (expected 1) — upstream format moved")
        pp = re.sub(
            r'"search_url": "\{google:baseURL\}search\?q=\{searchTerms\}[^"]*"',
            '"search_url": "http://127.0.0.1:9334/omnibox?q={searchTerms}"', pp)
        pp = re.sub(r'"suggest_url": "\{google:baseSuggestURL\}[^"]*"',
                    '"suggest_url": ""', pp)
        # The engine favicon was left pointing at Google; repoint it at Grok,
        # scoped to the now-"Grok" block (count=1) so no other engine is touched.
        pp = re.sub(
            r'("name": "Grok",[\s\S]{0,400}?"favicon_url": )"[^"]*"',
            r'\1"https://grok.com/favicon.ico"', pp, count=1)
        # Bump kCurrentDataVersion to current+1 so existing profiles re-merge.
        # (The old hardcoded "206"->"207" was a silent no-op on M151, which
        # already ships 207 — read the live value and increment it instead.)
        m = re.search(r'"kCurrentDataVersion":\s*(\d+)', pp)
        if not m:
            sys.exit("prepopulated_engines.json: kCurrentDataVersion not found")
        pp = re.sub(r'"kCurrentDataVersion":\s*\d+',
                    f'"kCurrentDataVersion": {int(m.group(1)) + 1}', pp, count=1)
        prepop.write_text(pp)
        print(f"  edited: {prepop}")

    # --- About page: "About Xplorer" + no failed-update error -------------
    grdp = src / "chrome/app/settings_chromium_strings.grdp"
    sg = grdp.read_text()
    if "About Xplorer" not in sg:
        sg = sg.replace(
            'desc="Menu title for the About Chromium page.">\n'
            "        About Chromium\n",
            'desc="Menu title for the About Chromium page.">\n'
            "        About Xplorer\n")
        sg = sg.replace(
            'desc="Text of the button which takes the user to the Chrome help'
            ' page.">\n        Get help with Chromium\n',
            'desc="Text of the button which takes the user to the Chrome help'
            ' page.">\n        Get help with Xplorer\n')
        sg = sg.replace(
            'desc="Status label: Already up to date (Chromium)">\n'
            "      Chromium is up to date\n",
            'desc="Status label: Already up to date (Chromium)">\n'
            "      Xplorer is up to date\n")
        grdp.write_text(sg)
        print(f"  edited: {grdp}")

    # --- About page version line: lead with the Xplorer version --------------
    # The about/help line is the Chromium ENGINE version ("Version 151.0.7897.0
    # (Developer Build) ..."). Prepend the Xplorer product version so users see
    # OUR version first. NOTE: bump XPLORER_VERSION here per release (or wire it
    # to the release version later).
    XPLORER_VERSION = "0.8.6"
    ss = src / "chrome/app/settings_strings.grdp"
    sst = ss.read_text()
    _ver_marker = "Xplorer " + XPLORER_VERSION + " · Chromium"
    if _ver_marker not in sst:
        # Bump-safe: matches a fresh checkout ("Version <ph>") OR an earlier
        # patched version ("Xplorer 0.6.1 · Chromium <ph>") and rewrites it to
        # the current version. (A plain string anchor breaks on version bumps
        # because the prior patch already consumed "Version <ph>".)
        sst, _n = re.subn(
            r'(?:Version|Xplorer [0-9][0-9.]* · Chromium) '
            r'(<ph name="PRODUCT_VERSION">)',
            _ver_marker + r" \1",
            sst, count=1)
        if _n:
            ss.write_text(sst)
            print(f"  edited (about version -> Xplorer {XPLORER_VERSION}): {ss}")

    # --- chrome/VERSION: monotonic PATCH so the Windows installer treats each
    # release as an UPGRADE. Upstream PATCH never changes (every build is
    # 151.0.7897.0), so setup.exe no-op'd a reinstall ("Higher version already
    # installed" / same-version repair) -> the installer appeared to do nothing.
    # Map XPLORER_VERSION "MAJ.MIN.PAT" -> PATCH = MIN*100 + PAT (0.8.6 -> 806),
    # well under the 16-bit VERSIONINFO / mac patch_hi-lo ceiling (65535).
    # chrome/VERSION is upstream (reverted by `git checkout -- .` each apply), so
    # the rewrite must live here, re-derived every apply.
    _xv = XPLORER_VERSION.split(".")
    _xpatch = int(_xv[1]) * 100 + int(_xv[2])
    version_file = src / "chrome/VERSION"
    vf = version_file.read_text()
    vf2 = re.sub(r"(?m)^PATCH=\d+$", f"PATCH={_xpatch}", vf)
    if vf2 != vf:
        version_file.write_text(vf2)
        print(f"  edited (chrome/VERSION PATCH -> {_xpatch}): {version_file}")

    # --- Windows install identity: "Chromium" -> "Xplorer" -------------------
    # Source of truth (chrome/install_static) for the install dir
    # (%LOCALAPPDATA%\<name>\Application), user-data dir, Software\<name> registry
    # root, Uninstall key, AppUserModelId, Default-Programs name, file-assoc
    # ProgIDs, and the elevation/tracing service NAMES. Without this, Xplorer
    # installed AS "Chromium" -> collided with real Chromium + shared its updater
    # identity (so reinstalls/updates targeted the same "Chromium"). install_static
    # is Windows-only (is_win in BUILD.gn) -> NEVER compiled on mac/linux, so a
    # malformed edit here is caught only by the Windows build, not the Mac pre-build.
    imh = src / "chrome/install_static/chromium_install_modes.h"
    t = imh.read_text()
    if 'kProductPathName[] = L"Xplorer"' not in t:
        t = t.replace('kProductPathName[] = L"Chromium";',
                      'kProductPathName[] = L"Xplorer";')
        t = t.replace('.base_app_name = L"Chromium",',
                      '.base_app_name = L"Xplorer",')
        t = t.replace('.base_app_id = L"Chromium",',
                      '.base_app_id = L"Xplorer",')
        t = t.replace('.browser_prog_id_prefix = L"ChromiumHTM",',
                      '.browser_prog_id_prefix = L"XplorerHTM",')
        t = t.replace('L"Chromium HTML Document",',
                      'L"Xplorer HTML Document",')
        t = t.replace('.pdf_prog_id_prefix = L"ChromiumPDF",',
                      '.pdf_prog_id_prefix = L"XplorerPDF",')
        t = t.replace('L"Chromium PDF Document",',
                      'L"Xplorer PDF Document",')
        # Active Setup GUID (system-level installs) -> fresh unique GUID.
        t = t.replace('L"{7D2B3E1D-D096-4594-9D8F-A6667F12E0AC}"',
                      'L"{63C1B345-8998-4A62-A654-70144D87282D}"')
        # Toast Activator CLSID -> fresh GUID 36DB671E-DF25-4F65-8C9B-963108D01396
        # (registered for USER installs too, so it MUST be unique vs Chromium's).
        t = re.sub(
            r"\.toast_activator_clsid = \{0x635EFA6F,.*?0x59\}\},",
            (".toast_activator_clsid = {0x36DB671E,\n"
             "                                  0xDF25,\n"
             "                                  0x4F65,\n"
             "                                  {0x8C, 0x9B, 0x96, 0x31, 0x08, 0xD0, 0x13,\n"
             "                                   0x96}},"),
            t, flags=re.DOTALL)
        imh.write_text(t)
        print(f"  edited (Windows install identity -> Xplorer): {imh}")

    # --- "Get help" links -> Xplorer GitHub (not Google support) -------------
    uc = src / "chrome/common/url_constants.h"
    uct = uc.read_text()
    if "github.com/daniel-farina/xplorer" not in uct:
        uct = uct.replace(
            '"https://support.google.com/chrome?p=help&ctx=settings"',
            '"https://github.com/daniel-farina/xplorer"')
        uct = uct.replace(
            '"https://support.google.com/chrome?p=help&ctx=menu"',
            '"https://github.com/daniel-farina/xplorer"')
        uc.write_text(uct)
        print(f"  edited (help -> GitHub): {uc}")

    # --- 3-dot menu + macOS menus: "About Chromium" -> "About Xplorer" -------
    cs = src / "chrome/app/chromium_strings.grd"
    cst = cs.read_text()
    if "About &amp;Xplorer" not in cst:
        # All 3 non-"for Testing" IDS_ABOUT bodies (use_titlecase, not-
        # use_titlecase, is_chromeos). The "Google Chrome for Testing" lines
        # are a different string and are intentionally left alone.
        cst = cst.replace("About &amp;Chromium", "About &amp;Xplorer")
        cs.write_text(cst)
        print(f"  edited (About Xplorer menus): {cs}")

    # macOS app-menu short name (IDS_APP_MENU_PRODUCT_NAME). Match the full
    # message tag so only this one "Chromium" body is touched.
    cst2 = cs.read_text()
    _app_menu_old = (
        '<message name="IDS_APP_MENU_PRODUCT_NAME" desc="The application\'s '
        "short name, used for the Mac's application menu, activity monitor, "
        "etc. This should be less than 16 characters. Example: Chrome, not "
        'Google Chrome." translateable="false">\n'
        "          Chromium\n"
        "        </message>"
    )
    _app_menu_new = (
        '<message name="IDS_APP_MENU_PRODUCT_NAME" desc="The application\'s '
        "short name, used for the Mac's application menu, activity monitor, "
        "etc. This should be less than 16 characters. Example: Chrome, not "
        'Google Chrome." translateable="false">\n'
        "          Xplorer\n"
        "        </message>"
    )
    if _app_menu_new not in cst2 and _app_menu_old in cst2:
        cst2 = cst2.replace(_app_menu_old, _app_menu_new)
        cs.write_text(cst2)
        print(f"  edited (app-menu product name): {cs}")

    # macOS app menu "About Chromium" (IDS_ABOUT_MAC). Upstream uses a $1
    # substitution of the product name, which is unpatched and would render
    # "About Chromium". Pin it to a literal "About Xplorer".
    gen = src / "chrome/app/generated_resources.grd"
    gent = gen.read_text()
    _about_mac_old = (
        '<message name="IDS_ABOUT_MAC" desc="The Mac menu item to open the '
        'about box.">\n'
        '          About <ph name="PRODUCT_NAME">$1<ex>Google Chrome</ex>'
        "</ph>\n"
        "        </message>"
    )
    _about_mac_new = (
        '<message name="IDS_ABOUT_MAC" desc="The Mac menu item to open the '
        'about box." translateable="false">\n'
        "          About Xplorer\n"
        "        </message>"
    )
    if "About Xplorer" not in gent and _about_mac_old in gent:
        gent = gent.replace(_about_mac_old, _about_mac_new)
        gen.write_text(gent)
        print(f"  edited (About Mac menu): {gen}")

    # --- About page license line: subject word only -------------------------
    # Keep the <ph>Chromium</ph> link text (correct attribution to the
    # upstream Chromium project); only swap the leading subject word.
    ccs = src / "components/components_chromium_strings.grd"
    ccst = ccs.read_text()
    if "Xplorer is made possible by the" not in ccst:
        ccst = ccst.replace(
            "Chromium is made possible by the",
            "Xplorer is made possible by the")
        ccs.write_text(ccst)
        print(f"  edited (license line): {ccs}")

    # macOS: Xplorer ships no Google updater, so the about page's update check
    # failed with "error code 0". Report up-to-date instead. (Live GitHub
    # release check deferred until the repo is public.)
    vum = src / "chrome/browser/ui/webui/help/version_updater_mac.mm"
    # macOS-only file: absent on Linux/Windows checkouts. Read as "" so the
    # anchored edits below all no-op (and never write) when it doesn't exist.
    vm = vum.read_text() if vum.exists() else ""
    _upd_pristine = (
        "  void CheckForUpdate(StatusCallback status_callback,\n"
        "                      PromoteCallback promote_callback) override {\n"
        "    updater::EnsureUpdater(\n"
        "        base::TaskPriority::USER_VISIBLE,\n"
        "        base::BindOnce(promote_callback, "
        "PromotionState::PROMOTE_ENABLED),\n"
        "        base::BindOnce(&updater::CheckForUpdate,\n"
        "                       base::BindRepeating(&UpdateStatus, "
        "status_callback)));\n"
        "  }")
    _upd_old = (
        "  void CheckForUpdate(StatusCallback status_callback,\n"
        "                      PromoteCallback promote_callback) override {\n"
        "    // XPLORER: Xplorer has no Google updater; skip the broken\n"
        "    // Keystone check (which reported \"error code 0\") and report\n"
        "    // up to date instead.\n"
        "    status_callback.Run(VersionUpdater::Status::UPDATED, 0, false,\n"
        "                        false, std::string(), 0, std::u16string());\n"
        "  }")
    # Live-check the Xplorer GitHub releases for a newer version (check-only;
    # no auto-download). Async NSURLSession; result posted back to the UI
    # sequence. "Update available" -> FAILED status carries a message+link.
    _upd_new = (
        "  void CheckForUpdate(StatusCallback status_callback,\n"
        "                      PromoteCallback promote_callback) override {\n"
        "    status_callback.Run(VersionUpdater::Status::CHECKING, 0, false,\n"
        "                        false, std::string(), 0, std::u16string());\n"
        "    scoped_refptr<base::SequencedTaskRunner> runner =\n"
        "        base::SequencedTaskRunner::GetCurrentDefault();\n"
        "    StatusCallback cb = status_callback;\n"
        "    NSURLSessionConfiguration* cfg =\n"
        "        [NSURLSessionConfiguration ephemeralSessionConfiguration];\n"
        "    cfg.timeoutIntervalForRequest = 10;\n"
        "    NSURLSession* session =\n"
        "        [NSURLSession sessionWithConfiguration:cfg];\n"
        "    NSURL* url = [NSURL URLWithString:\n"
        "        @\"https://api.github.com/repos/daniel-farina/xplorer/"
        "releases/latest\"];\n"
        "    NSURLSessionDataTask* task = [session dataTaskWithURL:url\n"
        "        completionHandler:^(NSData* data, NSURLResponse* response,\n"
        "                            NSError* error) {\n"
        "          std::string latest;\n"
        "          if (data && !error) {\n"
        "            NSDictionary* json = [NSJSONSerialization\n"
        "                JSONObjectWithData:data options:0 error:nil];\n"
        "            if ([json isKindOfClass:[NSDictionary class]]) {\n"
        "              NSString* tag = json[@\"tag_name\"];\n"
        "              if ([tag isKindOfClass:[NSString class]]) {\n"
        "                if ([tag hasPrefix:@\"v\"])\n"
        "                  tag = [tag substringFromIndex:1];\n"
        "                latest = base::SysNSStringToUTF8(tag);\n"
        "              }\n"
        "            }\n"
        "          }\n"
        "          VersionUpdater::Status status =\n"
        "              VersionUpdater::Status::UPDATED;\n"
        "          std::string out_version;\n"
        "          std::u16string message;\n"
        "          if (!latest.empty()) {\n"
        '            base::Version cur("' + XPLORER_VERSION + '");\n'
        "            base::Version newest(latest);\n"
        "            if (cur.IsValid() && newest.IsValid() &&\n"
        "                cur.CompareTo(newest) < 0) {\n"
        "              status = VersionUpdater::Status::FAILED;\n"
        "              out_version = latest;\n"
        "              message = base::UTF8ToUTF16(\n"
        '                  std::string("Xplorer v") + latest +\n'
        '                  " is available -- download from "\n'
        '                  "github.com/daniel-farina/xplorer/releases/latest");\n'
        "            }\n"
        "          }\n"
        "          runner->PostTask(\n"
        "              FROM_HERE, base::BindOnce(cb, status, 0, false, false,\n"
        "                                        out_version, int64_t{0},\n"
        "                                        message));\n"
        "        }];\n"
        "    [task resume];\n"
        "  }")
    if vum.exists() and "releases/latest" not in vm:
        if _upd_pristine in vm:
            vm = vm.replace(_upd_pristine, _upd_new)
        elif _upd_old in vm:
            vm = vm.replace(_upd_old, _upd_new)
        if '#include "base/strings/sys_string_conversions.h"' not in vm:
            vm = vm.replace(
                '#include "base/version.h"\n',
                '#include "base/version.h"\n'
                '#include "base/location.h"\n'
                '#include "base/strings/sys_string_conversions.h"\n'
                '#include "base/task/sequenced_task_runner.h"\n')
        vum.write_text(vm)
        print(f"  edited (live update check): {vum}")
    # Our CheckForUpdate no longer calls the UpdateStatus helper, which now
    # trips -Werror,-Wunused-function. Mark it maybe_unused.
    vm2 = vum.read_text() if vum.exists() else ""
    if vum.exists() and "[[maybe_unused]] void UpdateStatus" not in vm2:
        vm2 = vm2.replace(
            "\nvoid UpdateStatus(VersionUpdater::StatusCallback",
            "\n[[maybe_unused]] void UpdateStatus(VersionUpdater::StatusCallback")
        vum.write_text(vm2)
        print(f"  edited (maybe_unused): {vum}")

    # About-page "Learn more" link (shown next to our "update available" status,
    # which we deliver as a FAILED status) pointed at Google's stock update-error
    # help page. Repoint it to the Xplorer releases page so it links to our
    # download, not Google. (The other two learn-more links in about_page.html are
    # obsolete-OS and branded-only macOS promote — not shown in our build.)
    about_page = src / "chrome/browser/resources/settings/about_page/about_page.html"
    if about_page.exists():
        ap = about_page.read_text()
        if "github.com/daniel-farina/xplorer/releases/latest" not in ap:
            ap = ap.replace(
                'href="https://support.google.com/chrome?p=update_error"',
                'href="https://github.com/daniel-farina/xplorer/releases/latest"')
            about_page.write_text(ap)
            print(f"  edited (update learn-more URL): {about_page}")

    # Windows (unbranded, is_chrome_branded=false) compiles version_updater_basic
    # .cc, NOT version_updater_mac.mm. The basic updater reports DISABLED on the
    # About page; report up-to-date instead, matching the mac fix above. This is
    # best-effort: the exact upstream body varies across Chromium revisions, so
    # warn (don't fail the whole run) if the anchor isn't present — the basic
    # updater's DISABLED is cosmetic, not a hard error.
    vub = src / "chrome/browser/ui/webui/help/version_updater_basic.cc"
    if vub.exists():
        vb = vub.read_text()
        if "Run(UPDATED," in vb:
            print(f"  skip (already applied): {vub}")
        elif "Run(DISABLED," in vb:
            vb = vb.replace("Run(DISABLED,", "Run(UPDATED,", 1)  # XPLORER
            vub.write_text(vb)
            print(f"  edited: {vub}")
        else:
            print(f"  WARNING: {vub}: anchor 'Run(DISABLED,' not found; the "
                  "Windows About page may report 'updates disabled' (cosmetic)")

    # --- "Ask Google about this page" -> "Ask Grok about this page" ---------
    # Rebrand the Lens omnibox-action strings...
    omn = src / "components/omnibox_strings.grdp"
    og = omn.read_text()
    if "Ask Grok about this page" not in og:
        og = og.replace("Ask Google about this page", "Ask Grok about this page")
        og = og.replace("ask Google about this page", "ask Grok about this page")
        og = og.replace("Ask Google Lens about this page",
                        "Ask Grok about this page")
        og = og.replace("Ask Google Search about this page",
                        "Ask Grok about this page")
        omn.write_text(og)
        print(f"  edited: {omn}")
    # ...and rewire the action handler to open Grok instead of the Google Lens
    # overlay (the action routes 100% through OpenLensOverlay).
    acp = src / "chrome/browser/autocomplete/chrome_autocomplete_provider_client.cc"
    ac = acp.read_text()
    if "AskGrokAboutPage" not in ac:
        ac = ac.replace(
            '#include "chrome/browser/autocomplete/'
            'chrome_autocomplete_provider_client.h"',
            '#include "chrome/browser/autocomplete/'
            'chrome_autocomplete_provider_client.h"\n'
            '#include "chrome/browser/grok_companion/grok_companion_util.h"  '
            '// XPLORER', 1)
        ac = ac.replace(
            "void ChromeAutocompleteProviderClient::OpenLensOverlay(bool show) {\n"
            "#if !BUILDFLAG(IS_ANDROID)\n"
            "  if (auto* lens_search_controller =\n"
            "          GetLensSearchController(GetWebContents(web_contents_getter_))) {\n"
            "    if (show) {\n"
            "      // Force showing the contextual search box in the Lens Overlay.\n"
            "      lens_search_controller->OpenLensOverlay(\n"
            "          lens::LensOverlayInvocationSource::kOmniboxPageAction, true);\n"
            "    } else {\n"
            "      // TODO(crbug.com/402497756): For prototyping, reusing the existing\n"
            "      // omnibox entry point. However, for production, create a new invocation\n"
            "      // source for this new entry point.\n"
            "      lens_search_controller->StartContextualization(\n"
            "          lens::LensOverlayInvocationSource::kOmnibox);\n"
            "    }\n"
            "  }\n"
            "#endif  // !BUILDFLAG(IS_ANDROID)\n"
            "}",
            "void ChromeAutocompleteProviderClient::OpenLensOverlay(bool show) {\n"
            "  // XPLORER: \"Ask Grok about this page\" -> open Grok, not Google Lens.\n"
            "  grok_companion::AskGrokAboutPage(GetWebContents(web_contents_getter_));\n"
            "}")
        acp.write_text(ac)
        print(f"  edited: {acp}")

    # Arc-style vertical sidebar: "Tabs" section label + agent tab group.
    patch_vertical_sidebar(src)

    patch_xplorer_settings_access(src)

    # Bundle the Grok companion UI into the Windows installer's version dir
    # (next to chrome.dll) so the gateway's UiDir() resolves it via DIR_MODULE on
    # installed builds; without it the installed app's /search (and other UI
    # routes) return the gateway's 401. The version dir is fully extracted by
    # setup (a ChromeDir-root subdir is not), so it must live there. Windows-only
    # (mini_installer); harmless on macOS, which doesn't read this file. The
    # build/packaging step stages companion/ui into the out dir to be picked up.
    chrome_release = src / "chrome/installer/mini_installer/chrome.release"
    if chrome_release.exists():
        edit(
            chrome_release,
            "chrome_proxy.exe: %(ChromeDir)s\\\n",
            "chrome_proxy.exe: %(ChromeDir)s\\\n"
            "# XPLORER: companion UI in the version dir (gateway UiDir/DIR_MODULE).\n"
            "companion\\ui\\*.*: %(VersionDir)s\\companion\\ui\\\n",
        )

    print("Integration edits applied.")


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "../chromium/src"))
