# Grok Image Search — replacing the broken "Search this tab with Image Search"

**Branch:** `feat/grok-image-search` (off master) · **Started:** 2026-07-01

## Goal
Chrome's Lens "Search this tab with Image Search" / right-click "Search image with Google Lens"
captures an image then opens a Google page — DEAD in this fork (Google image search disabled;
the default engine was renamed Grok but its `image_url` still points at Google). Replace it with
**Grok image search**: capture the image → Grok vision analysis in a conversation.

## Key discovery (workflow wi5e01ix8)
- Grok is multimodal via the CLI `--prompt-json '[{type:image,data:base64,mimeType},{type:text,text}]'`.
  **Only `grok-composer-2.5-fast` has in-context vision**; `grok-build` is blind (treats the image as a
  missing file attachment and fails).
- The gateway ALREADY has the backend: `/api/screenshot` (base64 PNG of any tab, background-safe) +
  `/api/search/stream mode=images` + `ExtractSearchImage` + `BuildGrokSearchCommand` (has_image branch).
- **Bug:** `ResolveSearchModel`/`ResolveChatModel` default image requests to the blind `grok-build`
  (mode=images isn't in `SearchModeNeedsWebTools`) — must force Composer on `has_image`.
- **Gotcha:** the `mode=images` system prompt tells the model to "web-search for similar images," which
  Composer can't do → it derails. Workaround: phrase the query "answer directly from the image; do not
  use tools/web." The gateway also auto-compresses (image_compressed event) to fit the 2MB cap.

## Validated (no rebuild, via the live gateway)
- `/api/search/stream mode=images` + grok-composer described the user's screenshot correctly.
- Full flow: `POST /api/screenshot` (active tab, 456KB) → `mode=images` vision → accurate description
  ("Grok AI web interface… split layout… navigation for Search/New Chat/Imagine/Build… X trends…").

## Phase 0 — usable now, NO REBUILD  ✅ built
Sidebar image-search button (`companion/ui/index.html` + `app.js` `runImageSearch()`): captures the
current tab via `/api/screenshot`, streams a grok-composer vision answer via `/api/search/stream
mode=images` (forced model + no-tools query), renders it as a conversation with a thumbnail.
- Limitation: client-side only (the exchange isn't persisted to the conversation — the chat endpoint is
  text-only until Phase 1). Fine for immediate use.
- [ ] live click-test in a dev instance (`XPLORER_COMPANION_UI=repo` + test profile).

## Phase 1 — the actual native menu → Grok  (REBUILD, needs go-ahead)
- `patches/apply_integration.py`: replace `LensSearchController::OpenLensOverlay*` bodies (chromium
  `chrome/browser/ui/lens/lens_search_controller.cc`) with `grok_companion::GrokImageSearchForTab(...)`
  — covers the toolbar/app-menu "Search this tab with Image Search" + right-click region/image. Mirror
  the existing `chrome_autocomplete_provider_client.cc` body-replacement (~L2260). Optional: rebrand
  `IDS_LENS_OVERLAY_TAB_ENTRYPOINT_LABEL` → "Search this tab with Grok"; blank the engine's Google `image_url`.
- `grok_companion/grok_companion_util.{h,cc}`: `GrokImageSearchForTab()` → `OpenGrokSidePanel()` + set a
  pending-image-search flag (new tiny gateway endpoint), which the sidebar consumes on open (mirror
  `consumePendingApp` / `AskGrokAboutPage`).
- `grok_native.cc`: force `kComposerModel` when `has_image` in `ResolveSearchModel` + `ResolveChatModel`;
  add image support to `BuildGrokChatCommand` + the chat endpoint so the conversation persists the image.
- Probes before coding: confirm the `WebContents` accessor on `LensSearchController`; verify multi-turn
  vision (does `-p -r <session>` retain the first-turn image?).

## Files
- `companion/ui/app.js` (`runImageSearch`, `renderMessages` thumbnail) · `companion/ui/index.html` (#img-search)
- gateway: `agent_gateway/grok_native.cc` (screenshot 1991, ExtractSearchImage 2741, BuildGrokSearchCommand
  1051, ResolveSearchModel 600, chat 3918), `tab_screenshot.cc`
- Phase 1: `patches/apply_integration.py`, `grok_companion/grok_companion_util.{h,cc}`
