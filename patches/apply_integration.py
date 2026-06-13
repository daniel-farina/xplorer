#!/usr/bin/env python3
"""Applies Aether's integration edits to a Chromium checkout.

Idempotent: each edit checks for its marker before inserting. Anchors on
long-lived symbols (PreMainMessageLoopRun, chrome/browser deps block) so it
survives upstream churn better than context diffs.
"""
import re
import sys
from pathlib import Path

MARKER = "// AETHER"


def edit(path: Path, anchor: str, insertion: str, before: bool = False):
    text = path.read_text()
    # Idempotency: skip if the insertion's first non-empty line already exists.
    if insertion.strip().splitlines()[0] in text:
        print(f"  skip (already applied): {path}")
        return
    idx = text.find(anchor)
    if idx < 0:
        sys.exit(f"ANCHOR NOT FOUND in {path}: {anchor!r} — upstream moved; "
                 f"update apply_integration.py")
    pos = idx if before else idx + len(anchor)
    path.write_text(text[:pos] + insertion + text[pos:])
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
        '\n    "//chrome/browser/agent_gateway",  # AETHER',
    )

    # 3. Always start the CDP remote debugging server on 9333 — Aether treats
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
    # full speed while Aether is inactive, and capture hidden windows. Injected
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

    # 4. Branding: rename the product from "Chromium" to "XBrowser".
    branding = src / "chrome/app/theme/chromium/BRANDING"
    b = branding.read_text()
    if "PRODUCT_FULLNAME=XBrowser" not in b:
        b = b.replace("PRODUCT_FULLNAME=Chromium", "PRODUCT_FULLNAME=XBrowser")
        b = b.replace("PRODUCT_SHORTNAME=Chromium", "PRODUCT_SHORTNAME=XBrowser")
        b = b.replace("MAC_BUNDLE_ID=org.chromium.Chromium",
                      "MAC_BUNDLE_ID=org.xbrowser.XBrowser")
        branding.write_text(b)
        print(f"  edited: {branding}")

    # The visible app name comes from IDS_PRODUCT_NAME / IDS_SHORT_PRODUCT_NAME
    # in the (non-Google, non-CfT) else branch of chromium_strings.grd.
    grd = src / "chrome/app/chromium_strings.grd"
    g = grd.read_text()
    if ">\n            XBrowser\n" not in g:
        g = g.replace(
            'desc="The Chrome application name" translateable="false">\n'
            "            Chromium\n",
            'desc="The Chrome application name" translateable="false">\n'
            "            XBrowser\n",
        )
        g = g.replace(
            'desc="The Chrome application short name." translateable="false">\n'
            "            Chromium\n",
            'desc="The Chrome application short name." translateable="false">\n'
            "            XBrowser\n",
        )
        grd.write_text(g)
        print(f"  edited: {grd}")

    # 5. Link Grok companion (side panel + AI Mode redirect).
    edit(
        browser_gn,
        '\n    "//chrome/browser/agent_gateway",  # AETHER',
        '\n    "//chrome/browser/grok_companion",  # AETHER',
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
        '  // AETHER: Grok chip opens native Grok Search.\n'
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
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // AETHER\n',
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
        '  // AETHER: never open Google AI Mode — use native Grok Search.\n'
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
        '#include "chrome/browser/grok_companion/grok_companion_util.h"  // AETHER\n'
        '#include "chrome/browser/ui/browser.h"  // AETHER\n'
        '#include "chrome/browser/ui/omnibox/chrome_omnibox_client.h"  // AETHER\n',
    )
    edit(
        ai_mode_ctrl,
        'bool AiModePageActionController::ShouldShowPageAction(\n'
        '    Profile* profile,\n'
        '    LocationBarView& location_bar_view) {',
        'bool AiModePageActionController::ShouldShowPageAction(\n'
        '    Profile* profile,\n'
        '    LocationBarView& location_bar_view) {\n'
        '  // AETHER: always show Grok entrypoint in XBrowser.\n'
        '  if (profile && profile->IsRegularProfile()) {\n'
        '    return true;\n'
        '  }',
    )

    # Register Grok side panel in global entries.
    side_panel_helper = (
        src / "chrome/browser/ui/views/side_panel/side_panel_helper.cc")
    edit(
        side_panel_helper,
        '#include "chrome/browser/ui/views/side_panel/reading_list/reading_list_side_panel_coordinator.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // AETHER\n'
        '#include "chrome/browser/grok_companion/grok_web_bar.h"  // AETHER\n',
    )
    edit(
        side_panel_helper,
        '  // Add bookmarks.\n'
        '  BookmarksSidePanelCoordinator::From(browser)->CreateAndRegisterEntry(\n'
        '      window_registry);',
        '  // Add bookmarks.\n'
        '  BookmarksSidePanelCoordinator::From(browser)->CreateAndRegisterEntry(\n'
        '      window_registry);\n\n'
        '  // AETHER: Grok AI companion side panel + grok.com toolbar overlay.\n'
        '  grok_companion::RegisterGrokWebBar(browser);\n'
        '  grok_companion::RegisterGrokSidePanel(browser);',
    )

    # New tab page -> Grok search homepage.
    search_cc = src / "chrome/browser/search/search.cc"
    edit(
        search_cc,
        '    if (profile->IsOffTheRecord()) {\n'
        '      return NewTabURLDetails(GURL(), NEW_TAB_URL_INCOGNITO);\n'
        '    }',
        '    if (profile->IsOffTheRecord()) {\n'
        '      return NewTabURLDetails(GURL(), NEW_TAB_URL_INCOGNITO);\n'
        '    }\n\n'
        '    // AETHER: XBrowser uses Grok search as the default new tab page.\n'
        '    return NewTabURLDetails(grok_companion::GetDefaultSearchHomeURL(),\n'
        '                            NEW_TAB_URL_VALID);',
    )
    edit(
        search_cc,
        '#include "chrome/browser/search/search.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // AETHER\n',
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

    # Toolbar Grok button (opens same side panel as AI Mode chip).
    toolbar = src / "chrome/browser/ui/views/toolbar/toolbar_view.cc"
    edit(
        toolbar,
        '  overflow_button_ = AddChildView(std::make_unique<OverflowButton>());',
        '  // AETHER: Grok companion toolbar button.\n'
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
        '  }\n\n'
        '  overflow_button_ = AddChildView(std::make_unique<OverflowButton>());',
    )
    edit(
        toolbar,
        '#include "chrome/browser/ui/views/toolbar/toolbar_view.h"',
        '\n#include "chrome/browser/grok_companion/grok_companion_util.h"  // AETHER\n'
        '#include "chrome/browser/ui/views/toolbar/toolbar_button.h"\n'
        '#include "components/vector_icons/vector_icons.h"\n',
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

    print("Integration edits applied.")


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "../chromium/src"))
