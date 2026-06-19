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

- [ ] **Disable Variations/Finch + component updater + What's New** via the existing
  `chrome_main_delegate.cc` switch block (`apply_integration.py` ~217-233 / ~462-473).
  Add `disable-component-update`, `disable-field-trial-config`, empty `variations-server-url` +
  `variations-insecure-server-url`, and `ChromeWhatsNewUI` to disable-features.
  **ANCHOR CONSTRAINT:** the second edit anchors on the literal
  `AppendSwitchASCII("disable-features", "CalculateNativeWinOcclusion");` — if appending
  `ChromeWhatsNewUI` to that value, update the string in BOTH blocks or the second `edit()` aborts.

## 🕓 later (verify-then-fix; need a build or checkout to confirm)

- [ ] **Window/taskbar title says "… - Chromium"** — VISUALLY CONFIRMED on the test desktop
  (title bar read "Grok - Chromium"). Audit assumed handled; it is not. Find the residual product
  string / WM class.
- [ ] **User-Agent + UA-CH brand list** still "Chromium"/"Google Chrome"; append a `Xplorer/0.7.0`
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

## Notes

- Full audit reports (branding/search/privacy) captured by the `xplorer-branding-privacy-audit`
  workflow run `wf_6806647d-0ca`.
- The current search hijack reuses Chromium's built-in `id==1` (Google) slot → renames it to Grok.
  Side effect: Google is no longer separately selectable. Acceptable unless product wants it back.
