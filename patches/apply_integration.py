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

    print("Integration edits applied.")


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "../chromium/src"))
