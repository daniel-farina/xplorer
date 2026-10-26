# Degoogling — ungoogled-chromium integration

Xplorer borrows the **degoogling patches** from
[ungoogled-chromium](https://github.com/ungoogled-software/ungoogled-chromium) to strip the
remaining Google phone-home / telemetry / tracking channels out of the Chromium base, while
keeping Xplorer's own integration (Grok in every tab, the Agent Gateway, branding) fully
intact. This document explains exactly what was taken, what was deliberately left out, why,
and how to keep it current.

> **Scope decision:** we take the **patch series only**. We do **not** apply ungoogled's
> *domain substitution* (rewriting every Google domain to the `qjz9zk` placeholder) or its
> *binary pruning*. Those break legitimate Google sign-in / Chrome Web Store / Translate and
> carry a large per-bump maintenance cost, for little additional benefit to a browser that is
> meant to stay usable. Grok runs over `grok.com` / `x.com` (not Google domains) and is
> unaffected by anything here.

## The version-skew situation (read this first)

ungoogled-chromium patches target an **exact** Chromium version. Their newest release is
**149.0.7827.155-1**; they have shipped **no 150 and no 151**. Xplorer pins
**Chromium 151.0.7897.0** — roughly **two milestones ahead** of ungoogled.

Consequence: their 149-targeted patches cannot be expected to apply to 151 verbatim. We
therefore treat their 149 series as the **source of truth for content**, and select only the
patches that still apply to our 151 tree (verified with `git apply --check`). Patches that
have drifted are kept in-repo but **quarantined** (not applied) until they can be re-anchored
or until ungoogled ships a ≥150 tag we can refresh from.

The pins are recorded in [`/VERSIONS`](../VERSIONS).

## How it's wired

```
apply.sh / apply.ps1
  ├─ copy src/chrome, icons …
  ├─ patches/apply_integration.py        # Xplorer's Grok + branding edits (authoritative)
  └─ scripts/apply_ungoogled.sh          # ← degoogling pass, runs LAST
```

- The degoogling pass runs **after** `apply_integration.py` on purpose: Xplorer's Grok and
  branding edits land first and unconditionally; the ungoogled patches are best-effort on top.
- `scripts/apply_ungoogled.sh` (and the `.ps1` mirror) is **idempotent** (skips
  already-applied patches) and **non-fatal** (a patch that has drifted against the current pin
  is logged and skipped — it never breaks the overlay, since these patches only *remove*
  Google channels; a missing one degrades privacy coverage, it doesn't break Xplorer).
- Patches are **vendored verbatim** under `patches/ungoogled/`, preserving their upstream
  sub-paths (`core/…`, `extra/…`) as provenance, so they diff cleanly against future
  ungoogled refreshes. Source: ungoogled-chromium 149.0.7827.155-1
  (`2a53b77d33b055a4a41139007fb60b6ec1960a48`).

### Verify (read-only — never builds, never writes the tree)

```sh
scripts/check_ungoogled_patches.sh            # checks the ACTIVE series against ../chromium/src
scripts/check_ungoogled_patches.sh --all      # also reports the quarantine set
scripts/check_ungoogled_patches.sh /path/to/chromium/src --all
```

It runs `git apply --check` per patch and reports `APPLIES` / `FAILS`. Run it whenever you
bump the Chromium pin or refresh the vendored patches; it exits non-zero if any **active**
patch fails (suitable as a CI / pre-bump gate).

## What's applied — active series (24 patches)

All verified to apply cleanly to Chromium 151.0.7897.0 (after Xplorer's overlay). Source:
[`patches/ungoogled/series`](../patches/ungoogled/series). Three change behavior (not just
telemetry) and are marked **[B]**.

| Patch | Effect |
|-------|--------|
| `inox/0003-disable-autofill-download-manager` | No autofill crowdsourcing upload to Google |
| `inox/0015-disable-update-pings` | Empty updater Update/Ping URLs |
| `inox/0021-disable-rlz` | Compile out RLZ search-attribution tracking |
| `iridium/safe_browsing-disable-incident-reporting` | No Safe Browsing incident-report upload |
| `iridium/safe_browsing-disable-reporting-of-safebrowsing-over` | No client-side phishing verdict upload |
| `ungoogled/disable-crash-reporter` | No crash-dump upload to Google |
| `ungoogled/disable-fonts-googleapis-references` | Drop `fonts.googleapis.com` references |
| `ungoogled/disable-gaia` **[B]** | No browser-level Google sign-in / sync |
| `ungoogled/disable-gcm` | Disable Google Cloud Messaging (push) |
| `ungoogled/disable-network-time-tracker` | No `time.google` network-time ping |
| `ungoogled/disable-untraceable-urls` | Blank the RLZ financial-ping endpoint |
| `ungoogled/disable-webrtc-log-uploader` | No WebRTC log upload to Google |
| `ungoogled/doh-changes` **[B]** | Neutral DoH defaults (no Google auto-DoH) |
| `ungoogled/disable-mei-preload` | Drop Google Media-Engagement preload data |
| `ungoogled/remove-f1-shortcut` | F1 no longer opens `support.google.com` |
| `ungoogled/disable-domain-reliability` | No Google Domain Reliability uploads |
| `ungoogled/disable-profile-avatar-downloading` | No profile-avatar fetch from Google |
| `bromite/disable-fetching-field-trials` | Never fetch variations / field-trials from Google |
| `inox/0019-disable-battery-status-service` | Remove Battery Status API (fingerprinting surface) |
| `debian/google-api-warning` | Suppress the "Google API keys are missing" nag |
| `inox/0013-disable-missing-key-warning` | Companion missing-API-key warning suppression |
| `ungoogled/default-webrtc-ip-handling-policy` **[B]** | Default WebRTC IP policy that avoids local-IP leak |
| `ungoogled/disable-dial-repeating-discovery` | Stop repeating DIAL / Cast discovery |
| `ungoogled/disable-intranet-redirect-detector` | Stop random-hostname captive-portal DNS probes |

**Behavioral notes for review** — `disable-gaia` removes the browser's own *sign-in to
Chrome / sync to Google* (it does **not** affect "Sign in with Google" on websites);
`doh-changes` adjusts Secure-DNS defaults; `default-webrtc-ip-handling-policy` changes the
default WebRTC IP-handling mode. If any of these isn't wanted, delete its line from
`patches/ungoogled/series` (the file is the single source of what gets applied).

## What's added — GN flags

Build-safe degoogling switches from ungoogled's `flags.gn`, appended to all four
`build/args.gn*` files:

`enable_reporting=false`, `enable_remoting=false`, `enable_mdns=false`,
`enable_service_discovery=false`, `enable_hangout_services_extension=false`,
`disable_fieldtrial_testing_config=true`, `exclude_unwind_tables=true`.

Five ungoogled flags (`use_official_google_api_keys=false`, the three empty
`google_*` keys, `enable_rlz=false`) were **already** set by Xplorer.

> **Held back: `safe_browsing_mode=0`.** This is the single biggest degoogling flag, but
> setting it strips the Safe Browsing subsystem and requires ungoogled's
> `*-fix-building-without-safebrowsing` companion patches — which currently **fail** on 151
> (they're quarantined). Enabling the flag without them would break the link. Runtime Safe
> Browsing *reporting* is already removed by the two iridium patches in the active series, so
> we keep the privacy win without the build risk. Graduate the flag once those patches are
> re-anchored.

## What's quarantined (vendored, NOT applied — 10 patches)

See [`patches/ungoogled/quarantine.series`](../patches/ungoogled/quarantine.series). Reasons:

- **Drift (fail on 151, need a re-anchor):** `disable-privacy-sandbox`,
  `toggle-translation-via-switch`, `remove-unused-preferences-fields`,
  `disable-google-host-detection` (also touches suggest plumbing near the Grok omnibox —
  reconcile carefully), `disable-fedcm-by-default`, `inox/0006-modify-default-prefs`.
- **Coupled to the held-back flag:** both `*-fix-building-without-safebrowsing` (only needed
  with `safe_browsing_mode=0`).
- **Capability tradeoffs:** `disable-default-extensions`, `extensions-manifestv2`, and
  `disable-webstore-urls` (this last one *applies cleanly* but removes Chrome Web Store
  access, which conflicts with Xplorer's "every extension runs" positioning — opt in only if
  you want full Web Store removal).

## What's deliberately excluded (not vendored)

- **Domain substitution** (`qjz9zk`) and **binary pruning** — out of scope (see top).
- **`replace-google-search-engine-with-nosearch`** and other **search / new-tab / branding**
  patches — they rewrite the same `prepopulated_engines.json` "google" block and NTP/branding
  that `patches/apply_integration.py` already rewrites to make **Grok** the default. They are
  mutually exclusive with Xplorer's integration; Xplorer already achieves "no Google search"
  via its own Grok edit.
- ungoogled's **chrome://flags UX toggles** (fingerprinting/referrer/etc. flag patches) — they
  depend on ungoogled's flag-header infrastructure and are UX, not core degoogling. Candidates
  for a later pass.

## Keeping it updated

ungoogled's own maintenance model (for reference): patches are a GNU **quilt** series pinned
to an exact Chromium version via `chromium_version.txt` + `revision.txt`; on a Chromium bump a
maintainer runs `quilt push -a --refresh` to rebase the whole series, fixes breakages one
patch at a time, and validates with `devutils/validate_patches.py` (no-fuzz apply against a
clean tree). Releases are tagged `{chromium_version}-{revision}`; an hourly Action watches
Google's version history and files a tracking issue when stable moves. Updating is volunteer /
best-effort, so they trail Chromium stable by days-to-weeks (today: a full ~2 milestones).

**Xplorer's process** (lightweight analog — `scripts/check_ungoogled_patches.sh` is our
`validate_patches.py`):

1. **On every Chromium pin bump:** re-run `scripts/check_ungoogled_patches.sh --all`. Any
   active patch that newly `FAILS` moves to `quarantine.series` (coverage degrades *visibly*
   in review, never silently). Put the `applies N / quarantined M` line in the PR.
2. **Reconcile high-value drift by hand** when needed before ungoogled catches up: re-anchor
   the patch against `../chromium/src`, keep the reconciled copy under `patches/ungoogled/`
   with a `reconciled-from ugc 149, pending upstream` note, and re-run the check.
3. **Graduate on an ungoogled ≥150/151 tag:** when ungoogled ships a tag at/after our
   milestone, diff their refreshed `core/ungoogled-chromium/` series against our vendored copy,
   replace our hand-reconciled patches with their official ones, re-run the check, clear the
   matching quarantine entries, and bump `ungoogled =` in `/VERSIONS`.
4. **Watch upstream:** periodically check
   <https://github.com/ungoogled-software/ungoogled-chromium/tags> for a tag whose
   `chromium_version` ≥ our pin's milestone (mirrors ungoogled's own `new_version_check`).

## Credit & license

The patches under `patches/ungoogled/` are from
[ungoogled-chromium](https://github.com/ungoogled-software/ungoogled-chromium), which is
distributed under the **BSD-3-Clause** license — the same family as Chromium's. The upstream
license text is vendored alongside the patches at
[`patches/ungoogled/LICENSE.ungoogled`](../patches/ungoogled/LICENSE.ungoogled). With thanks
to the ungoogled-chromium project and the upstream patch sets it draws on (Inox, Bromite,
Iridium, Debian).
