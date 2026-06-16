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
        f"\n  {MARKER}: AI-native defaults — never throttle backgrounded tabs.\n"
        "  {\n"
        "    base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();\n"
        '    for (const char* sw : {"disable-renderer-backgrounding",\n'
        '                           "disable-backgrounding-occluded-windows",\n'
        '                           "disable-background-timer-throttling"}) {\n'
        "      if (!cmd->HasSwitch(sw))\n"
        "        cmd->AppendSwitch(sw);\n"
        "    }\n"
        '    if (!cmd->HasSwitch("disable-features"))\n'
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion");\n'
        "  }\n",
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
        branding.write_text(b)
        print(f"  edited: {branding}")

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

    # Enable AI Mode omnibox entrypoint feature flag.
    edit(
        cmd_delegate,
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion");\n'
        '  }\n',
        '      cmd->AppendSwitchASCII("disable-features",\n'
        '                             "CalculateNativeWinOcclusion");\n'
        '    if (!cmd->HasSwitch("enable-features"))\n'
        '      cmd->AppendSwitchASCII("enable-features",\n'
        '                             "AiModeOmniboxEntryPoint");\n'
        '  }\n',
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

    # Toolbar Grok button (opens local search page; Grok logo icon).
    toolbar = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    grok_btn_block = (
        '  // XPLORER: Grok companion toolbar button.\n'
        '  {\n'
        '    auto grok_btn = std::make_unique<ToolbarButton>(base::BindRepeating(\n'
        '        [](Browser* b) {\n'
        '          if (b)\n'
        '            grok_companion::OpenGrokSearchPage(b);\n'
        '        },\n'
        '        base::Unretained(browser_)));\n'
        '    grok_btn->SetTooltipText(u"Grok Search");\n'
        '    grok_btn->SetAccessibleName(u"Grok Search");\n'
        '    grok_btn->SetVectorIcon(kGrokIcon);\n'
        '    grok_btn->SetProperty(views::kElementIdentifierKey,\n'
        '                          kToolbarGrokButtonElementId);\n'
        '    AddChildView(std::move(grok_btn));\n'
        '  }\n\n'
    )
    if "kGrokIcon" not in toolbar.read_text():
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
            edit(
                toolbar,
                '  overflow_button_ = AddChildView(std::make_unique<OverflowButton>());',
                grok_btn_block +
                '  overflow_button_ = AddChildView(std::make_unique<OverflowButton>());',
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
        pp = pp.replace(
            '"name": "Google",\n      "keyword": "google.com",',
            '"name": "Grok",\n      "keyword": "grok.com",')
        pp = re.sub(
            r'"search_url": "\{google:baseURL\}search\?q=\{searchTerms\}[^"]*"',
            '"search_url": "http://127.0.0.1:9334/omnibox?q={searchTerms}"', pp)
        pp = re.sub(r'"suggest_url": "\{google:baseSuggestURL\}[^"]*"',
                    '"suggest_url": ""', pp)
        pp = pp.replace('"kCurrentDataVersion": 206',
                        '"kCurrentDataVersion": 207')
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

    # macOS: Xplorer ships no Google updater, so the about page's update check
    # failed with "error code 0". Report up-to-date instead. (Live GitHub
    # release check deferred until the repo is public.)
    vum = src / "chrome/browser/ui/webui/help/version_updater_mac.mm"
    vm = vum.read_text()
    if "Xplorer has no Google updater" not in vm:
        vm = vm.replace(
            "  void CheckForUpdate(StatusCallback status_callback,\n"
            "                      PromoteCallback promote_callback) override {\n"
            "    updater::EnsureUpdater(\n"
            "        base::TaskPriority::USER_VISIBLE,\n"
            "        base::BindOnce(promote_callback, "
            "PromotionState::PROMOTE_ENABLED),\n"
            "        base::BindOnce(&updater::CheckForUpdate,\n"
            "                       base::BindRepeating(&UpdateStatus, "
            "status_callback)));\n"
            "  }",
            "  void CheckForUpdate(StatusCallback status_callback,\n"
            "                      PromoteCallback promote_callback) override {\n"
            "    // XPLORER: Xplorer has no Google updater; skip the broken\n"
            "    // Keystone check (which reported \"error code 0\") and report\n"
            "    // up to date instead.\n"
            "    status_callback.Run(VersionUpdater::Status::UPDATED, 0, false,\n"
            "                        false, std::string(), 0, std::u16string());\n"
            "  }")
        vum.write_text(vm)
        print(f"  edited: {vum}")
    # Our CheckForUpdate no longer calls the UpdateStatus helper, which now
    # trips -Werror,-Wunused-function. Mark it maybe_unused.
    vm2 = vum.read_text()
    if "[[maybe_unused]] void UpdateStatus" not in vm2:
        vm2 = vm2.replace(
            "\nvoid UpdateStatus(VersionUpdater::StatusCallback",
            "\n[[maybe_unused]] void UpdateStatus(VersionUpdater::StatusCallback")
        vum.write_text(vm2)
        print(f"  edited (maybe_unused): {vum}")

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

    print("Integration edits applied.")


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "../chromium/src"))
