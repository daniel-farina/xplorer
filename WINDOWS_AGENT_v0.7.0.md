Task: build and publish the Windows x64 artifact for Xplorer v0.7.0

CONTEXT

The macOS side merged the new native Chromium toolbar (the Grok pill strip is now a real browser-chrome bar between the bookmark bar and the page, not a web overlay) plus a gateway toolbar-config API, into your Windows build work. The result is on branch feat/native-toolbar and the in-app version is bumped to 0.7.0.

A draft GitHub release v0.7.0 already exists (draft = not "latest", and no git tag is created until we publish). Your job: produce the Windows x64 portable zip from the merged branch and upload it to that draft. Do not publish it.

The toolbar is platform-neutral C++ (Views, base, net) and reuses existing compiled Chromium vector icons, so there are no new icon files and nothing macOS-only. apply.ps1 already copies the new src\chrome\...\xplorer\ files and runs the same cross-platform patches\apply_integration.py, so the toolbar wires itself into the Windows build with no extra steps.

STEP 1 — sync the merged branch

In the Xplorer repo (your Chromium checkout is already present from the prior build). Adjust the path to your repo:

cd C:\src\xplorer
git fetch origin
git checkout feat/native-toolbar
git pull

No gclient sync is needed — this change does not touch DEPS or the Chromium version pin, so your existing ..\chromium\src checkout is fine. Just re-apply and rebuild.

STEP 2 — build and package (apply, build, package in one command)

.\scripts\release_win.ps1 -Arch x64 -Version v0.7.0

Artifacts land in xplorer\dist\ : Xplorer-windows-x64.zip and Xplorer-windows-x64.sha256.txt . (Optional: add -Installer to also produce mini_installer.exe.)

STEP 3 — upload to the draft release v0.7.0

gh release upload v0.7.0 dist\Xplorer-windows-x64.zip dist\Xplorer-windows-x64.sha256.txt --clobber

SANITY CHECK before uploading

Launch the built Xplorer.exe and confirm: a native pill toolbar renders below the bookmark bar (icons, pills); curl http://127.0.0.1:9334/toolbar returns JSON like {"toolbar":{"pills":[...]}} ; a pill with a dropdown (e.g. Grok Build) shows an integrated down-caret that opens a menu; right-click a pill shows Customize / Hide / Edit / Remove; pills can be dragged to reorder.

IF SOMETHING FAILS — report back, do not work around it

1) If apply.ps1 prints "ANCHOR NOT FOUND ...": the toolbar integration patch expects a Chromium source line your checkout's version doesn't have (version drift). Paste the exact anchor string from the error. The macOS side will adjust patch_native_toolbar in patches/apply_integration.py (it is cross-platform, so the fix covers both OSes), then you git pull and retry.

2) If you hit compile errors in any of these files: src/chrome/browser/ui/views/xplorer/xplorer_toolbar_view.cc and .h ; src/chrome/browser/ui/views/xplorer/xplorer_toolbar_pill_button.cc and .h ; src/chrome/browser/ui/views/xplorer/xplorer_toolbar_icons.cc and .h ; src/chrome/browser/agent_gateway/grok_native.cc ; src/chrome/browser/grok_companion/grok_companion_util.cc and .h — paste the full error output. The code is cross-platform but the macOS side cannot compile-check Windows, so fixes are made from the error text; then git pull and rebuild.

AFTER THIS

Once the Windows zip is on the draft and macOS testing is done, the macOS side merges feat/native-toolbar into master and publishes v0.7.0 as the latest release. Until then, leave the draft unpublished.
