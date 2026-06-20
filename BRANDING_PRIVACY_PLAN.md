# Xplorer Branding + Privacy Plan (`branding` branch)

Living roadmap for the hourly branding/privacy loop. Goals:
1. **Branding** — all Chromium/Google Chrome → Xplorer.
2. **Search** — default search engine = Grok, not Google.
3. **Privacy** — Brave/ungoogled-style: no data sent to Google.

Source of truth is the overlay: `patches/apply_integration.py` (anchor-based `edit()`),
`src/chrome/**` (verbatim file copies), and `build/args.gn*` (GN flags, copied as-is by `build.sh`).

Each loop iteration: read this file + `git log`, do the next safe item, commit, tick it off.
**Never push to master.** Keep edits idempotent so `apply.sh` re-applies cleanly.

---

## ✅ Done

- [x] **Privacy GN args** in all four `build/args.gn*` (purely additive, verified net-new):
  `use_official_google_api_keys=false`, empty `google_api_key`/`client_id`/`client_secret`,
  `enable_reporting=false` (UMA/UKM/crash upload), `safe_browsing_mode=0` (no /v4 lookups+pings),
  `enable_hangout_services_extension=false`, `enable_mdns=false`, `enable_rlz=false`.
  *(NOT touching `enable_widevine` — keep DRM; NOT `is_official_build`.)*
- [x] **Blank Google key env vars** in `build.sh` before `gn gen` (belt-and-suspenders so a dev's
  exported real key can't be compiled in).

## ✅ Done (cont.)

- [x] **Hardened the prepopulated_engines.json default-search hijack** (`apply_integration.py`).
  Replaced the literal `.replace()` (silent no-op on drift) with a whitespace-tolerant `re.subn`
  + `n==1` **fail-loud assert**, and the hardcoded `kCurrentDataVersion 206→207` (a no-op on M151,
  which ships 207) with a **dynamic read-then-+1 bump**. Verified the exact pristine name/keyword
  format + version against the **local Mac checkout** (same pinned rev 242a04c8 — no snapshot needed)
  and unit-tested the transform (Grok set, 207→208, other engines untouched, idempotent, valid JSON).
- [x] **Grok engine favicon** — was still Google's `googleg_alldp.ico`; now `https://grok.com/favicon.ico`,
  scoped `count=1` to the Grok block.

## 🔜 do_now (next iterations, in order)

- [x] **Disable Variations/Finch + component updater** — DONE (branding-phase2): added
  `disable-component-update`, `disable-field-trial-config`, and empty `variations-server-url` +
  `variations-insecure-server-url` to the BasicStartupComplete switch block in
  `chrome_main_delegate.cc`. Applies cleanly (both cmd_delegate edits intact) + built (arm64 compiles).
- [x] **Disable What's New (`ChromeWhatsNewUI`)** — DONE (branding-phase2): added ChromeWhatsNewUI
  to the disable-features value in all 3 places (block-1 insertion + block-2 anchor + insertion) so
  both cmd_delegate edits stay matched; applies cleanly, AiMode enable-features intact. Was: split out of the above. Appending it to the
  existing `disable-features` value is anchor-fragile: the second cmd_delegate `edit()` anchors on
  `AppendSwitchASCII("disable-features", "CalculateNativeWinOcclusion");`, so the string must change
  in BOTH blocks together or the second edit aborts. Do as its own careful step.

## 🕓 later (verify-then-fix; need a build or checkout to confirm)

- [x] **Broad app-name rebrand in chromium_strings.grd** — DONE + BUILD-VERIFIED (branding-phase2):
  ~700 hardcoded "Chromium" user strings (default-browser prompt, profile/startup errors,
  background-run, update nags) → Xplorer, preserving the "Chromium Authors" copyright. Risk-checked
  (no message-name IDs contain "Chromium"); compiles clean (arm64, 3m12s); built pak confirms
  "Make Xplorer the default" etc.
- [x] **Remaining "Chromium" strings in OTHER grds** — DONE + BUILD-VERIFIED (branding-phase2):
  generalized the safe rebrand into `rebrand_grd_strings()` (skips google_chrome variants + any file
  with "Chromium" in message-name IDs; preserves "Chromium Authors") and globbed all *strings.grd/.grdp
  across chrome/app + components + generated_resources.grd. 22 files rebranded (~300 strings: settings,
  omnibox pedals, privacy sandbox, password manager, page info, autofill, SSL/security interstitials,
  version UI, …). Compiles clean (arm64, 3m15s). Only 3 "Chromium" left: 2 copyright + 1 intentional
  about-page engine-version line ("Xplorer 0.7.4 · Chromium <ver>"). Was: the built locale.pak still shows
  "Chromium blocks/sends/asks/uses/recommends…" sourced from generated_resources.grd + component
  *_strings.grd files (NOT chromium_strings.grd). Apply the same risk-checked broad replace (preserve
  message-name IDs + "Chromium Authors") to those grds. Biggest remaining branding surface.


- [x] **Window/taskbar title says "… - Chromium"** — FIXED: the title-format messages
  (IDS_BROWSER_WINDOW_TITLE_FORMAT + accessible/channel + ChromeOS/captive-portal variants)
  hardcoded "- Chromium" rather than the renamed product; rebranded all to Xplorer. Was VISUALLY CONFIRMED on the test desktop
  (title bar read "Grok - Chromium"). Audit assumed handled; it is not. Find the residual product
  string / WM class.
- [x] **User-Agent + UA-CH brand list** — DONE (branding-phase2): append `{"Xplorer", version}` to
  the shared brand-list builder in user_agent_utils.cc (before the ShuffleBrandList return), so both
  the low-entropy and full-version Sec-CH-UA lists advertise Xplorer. Chromium kept, Chrome/<ver>
  untouched (site-compat). Apply-verified (1 insert, Chromium intact); compile confirmed by the
  next build. Was: append a `Xplorer/0.7.0`
  brand token (never replace `Chrome/<ver>` — site-compat). `chrome_content_client.cc` +
  `user_agent_utils.cc`; verify M151 signatures first.
- [ ] **Force kill-switch prefs** in a PostBrowserStart hook: `kMetricsReportingEnabled=false`,
  `kDnsOverHttpsMode="off"`, `kNetworkPredictionOptions=2`, `kSigninAllowed=false`.
- [ ] **First-run / Welcome / What's New** still say Chromium — prefer disabling the feature.
- [ ] **macOS helper bundles / Info.plist / crashpad product** — verify against a build; likely
  already covered by product-name + bundle-id edits.
- [ ] **Linux product_logo PNGs + About-page logo** — `apply.sh` skips logo regen on non-Darwin;
  ship pre-rendered Xplorer PNGs.
- [ ] **Windows installer / ARP / shortcut strings** — grep `chrome/installer` after apply.
- [ ] **AgentGateway port** — `search_url` hardcodes `127.0.0.1:9334`; confirm `Start(0)` pins 9334
  (else omnibox search 404s) and make `/omnibox` 302 even with an empty store.
- [x] **macOS `UTTypeDescription` = "Chromium Extension" / "Chromium Shortcut"** — FIXED (branding-phase2:
  routed through `${CHROMIUM_SHORT_NAME}`; generated bundle now reads "Xplorer Extension/Shortcut"). Was VERIFIED in the
  shipped **v0.7.3** bundle (`/Applications/Xplorer.app/Contents/Info.plist`; user-visible in Finder
  Get-Info on `.crx`/app-shortcut files). Should read "Xplorer …". Source: the mac app Info.plist is
  generated from `chrome/app/app-Info.plist` / branding strings — fix via an `apply_integration.py`
  Info.plist string replacement (needs a build to confirm the generated plist picks it up).
- [ ] **macOS `CFBundleShortVersionString` = engine version (151.0.7897.0), not 0.7.3** — Finder
  Get-Info shows the Chromium version. Cosmetic/conventional (Chrome does the same), but if we want
  the Xplorer version surfaced here, set it from `XPLORER_VERSION` during apply. Low priority.

## Audit log
- **2026-06-20 (post-v0.7.3):** audited the shipped arm64 bundle. ✅ Correct: helper apps
  ("Xplorer Helper (…)"), framework ("Xplorer Framework"), `CFBundleName`/`DisplayName` = Xplorer,
  identifier `org.xplorer.Xplorer`. ❌ Residual: the two `UTTypeDescription` strings above. Internal
  binaries `chrome_crashpad_handler` (+ `app_mode_loader`) keep Chromium names but are not user-visible.

## Notes

- **2026-06-20 (phase2 build-verify):** the full phase2 overlay — UTType, Variations/Finch/
  component-updater off, window-title rebrand, What's New off, UA brand token, + the merged v0.7.4
  sidebar — **compiles clean on arm64 (7m16s)**. All previously apply-verified branding items are now
  build-verified. Ready to ship in a v0.7.5.

- Full audit reports (branding/search/privacy) captured by the `xplorer-branding-privacy-audit`
  workflow run `wf_6806647d-0ca`.
- The current search hijack reuses Chromium's built-in `id==1` (Google) slot → renames it to Grok.
  Side effect: Google is no longer separately selectable. Acceptable unless product wants it back.
