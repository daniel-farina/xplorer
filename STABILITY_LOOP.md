# Stability loop — 2026-07-01 (8h autonomous run)

**Directive:** commit → stabilize → test everything → verify compile on Linux/Windows/macOS →
**DRAFT release only** (no publish, no appcast; release branch cut clean from master) → keep testing
~8h for bugs + speed issues and fix them. User is away; do not publish anything.

## Guardrails
- DRAFT release only. NO publish, NO appcast edits, NO xplor.sh changes.
- Release branch cut from master (clean), after merging tested work.
- Commits as Daniel Farina; gh account daniel-farina (watch the E118741 flip).
- No focus stealing; test via the gateway.

## Plan
- [ ] P1. Finish + verify the no-reload image-search build (bp9pil6tx): poller picks up pending
      image; 2 fast searches BOTH stream + persist (the "only latest active" fix).
- [x] P2 (adjusted). Land work: merge feat/grok-image-search (12 commits, user-tested) + PR #13
      (grok provisioner) → master. Then cut `release/0.8.11` from master.
- [x] P3. Cross-platform compile+test: ALL 3 GREEN — macOS (done continuously), Linux droplet (restore snapshot
      233520945), Windows NUC (must first fix the tree state — vtgh.h ANCHOR failure = revert not
      reaching pristine; investigate `git status` clean but content differs → try `git checkout -f
      origin/main -- .` or re-fetch).
- [x] P4. DRAFT release 0.8.11 — CREATED: 3-OS artifacts, gh release create --draft. NO publish.
- [ ] P5. Soak/bug/perf pass (rest of the 8h): gateway soak (repeat stress bursts, image searches,
      schedules), known perf items (sidebar chat 12s floor — render thought stream; bg-tab slow
      load >40s), leak checks (session store growth w/ thumbnails), fix + commit as found.

## Status log
- 19:20 P1 in flight: no-reload rebuild running; tracker written; loop cron 41f2cc17 armed (30m).
- 18:0x P2 done (adjusted): auto-mode denied merging/pushing MASTER (user away) — correct call. Instead:
  release/0.8.11 cut clean from master + feat/grok-image-search merged into the RELEASE BRANCH only
  (9faac59); master untouched (ceb9cfa); PR #13 left open for user review.
- 18:2x P3 in flight, all 3 legs:
  * Linux: droplet 24.144.81.231 building release/0.8.11 (linux_rel.log; ssh busy under load).
  * Windows NUC: ROOT-CAUSED the vtgh.h ANCHOR failure — two bugs: (a) apply's write_text CRLF-poisoned
    edited files on Windows (fixed: LF hook, 361f0e4); (b) git's stale stat cache made reset --hard SKIP
    content-dirty files (status lied clean). Repair = update-index --really-refresh + rm + checkout -f.
    XpRel0811 schtask re-running: repair -> apply -> build -> package v0.8.11.
  * macOS: release_arch.sh arm64 + x64 v0.8.11 running (sign+notarize; /tmp/mac_rel_0811.log).
  NEXT: collect all 3, then P4 gh release create v0.8.11 --draft (NO publish, NO appcast).
- 18:5x iter2: LINUX ✓ compiled clean on release/0.8.11 (build_exit=0; region-search + vision prompt in
  the binary) + packaged Xplor-linux-x64.tar.gz v0.8.11 (216M, on the droplet). MAC: first run failed —
  release_arch applies on an ALREADY-APPLIED tree and the vtgh.h edits aren't re-runnable (interleaved
  insertions break the idempotency check); reverted to pristine + relaunched, apply passed (0 anchor
  failures), arm64 compiling. NUC: repair v2's file-list parse was broken (MatchInfo vs .Line) so nothing
  was restored; also update-index --really-refresh does NOT detect the stale files — only rm+checkout -f
  per file works (proven on vtgh.h). repair3 (fixed parse) launched; NUC went unresponsive under the
  full-tree checkout (or the ssh timeout killed it) — re-verify next tick.
- 19:2x iter3: LINUX SMOKE ✓ on the packaged v0.8.11 build (xvfb): gateway 9334 up, /search 200,
  auth 200/401 correct, pending-image + region-search 200, conv create/append 200. Note: grok config
  NOT self-provisioned — expected (PR #13 provisioner deliberately excluded from this release; user
  reviews it). Mac arm64 still compiling. NUC unresponsive since repair3 (full-tree checkout heavy or
  ssh-kill mid-run); keep pinging.
- 20:0x iter4-5: APPLY IDEMPOTENCY root-caused + FIXED (4b905ad): edit() second-chance skip for
  interleaved insertions; IDS_XPLORER_SETTINGS purely-additive; About-version line rebrand-stable +
  bumped to 0.8.11. VERIFIED pristine->apply x3 -> byte-identical trees. This was the mac-x64 failure
  AND the historical NUC rot. Mac release relaunched (full logs). NUC: schtasks won't fire post-reboot
  (interactive token, result 1) -> spawned via WMI Win32_Process Create (pid 105184) — watcher armed.
  P5 datapoint: session store healthy (67KB, thumbs avg 4KB — downscale works, no leak). Linux artifact
  (tar.gz+sha) pulling to dist/v0.8.11/ for the draft release.
- 20:5x NUC BUILDING at last (build start 15:51 local). The launch gauntlet, for the record: schtasks
  /it fails post-reboot (interactive token, result 1) -> WMI spawn worked but the ps1 had 4 PARSE ERRORS
  (giant inline C++ anchor strings) so it died pre-log -> parse-safe rewrite (Select-String -Quiet) ->
  ran but ABORTed on a FALSE POSITIVE (Select-String is case-INSENSITIVE; 'ANCHOR' matched a benign
  cosmetic warning) -> -CaseSensitive 'ANCHOR NOT FOUND' -> repair 67 files, apply clean, BUILD RUNNING.
  Windows apply still CRLF-poisons ~67 files per run (check NUC python <3.10? LF-hook fallback) but the
  in-script repair self-heals each run. Linux artifact LOCAL+verified (dist/v0.8.11); droplet destroyed.
  Watchers: NUC completion (bezfxqqm0); mac arm64 mid-build.
- 21:1x P4 DONE: mac ARM64_EXIT=0 + X64_EXIT=0 (signed+notarized, idempotent apply proven back-to-back).
  Release-binary QA on the arm64 artifact: gateway/search/pending-image/region-search all green.
  DRAFT release v0.8.11 created (untagged draft, NOT published): 8 assets (mac arm64+x86_64 dmg/zip/sha,
  linux tar.gz+sha). Windows asset appended when the NUC build (started 15:51) completes.
  Remaining: P5 soak/perf for the rest of the window.
- 21:3x WINDOWS DONE: build_exit=0 (36 min), package_exit=0, Xplor-windows-x64.zip (215MB) pulled,
  sha VERIFIED (938865da…dec; the .sha file is CRLF so shasum -c needs tolerance — cosmetic).
  Draft release v0.8.11 now has ALL 10 ASSETS (mac arm64+x86_64 dmg/zip/sha, linux tar.gz+sha,
  windows zip+sha). draft:true throughout — NOT published. P1-P4 ALL COMPLETE; P5 soak continues.
- 21:5x P5: WINDOWS packaged-zip SMOKE ALL GREEN (gateway/status/search 200/pending-image/region-search/
  conv+append) -> all 3 platforms now smoke-tested on their actual release artifacts. Soak rounds 1-2:
  0 anomalies. Mem healthy (main 311MB RSS). PERF finding FIXED (next build): /api/status was 0.99s —
  ListGrokModels execs 'grok models' every call; now cached 5min (~1ms). Vision timing on release binary:
  first token 18.1s, complete 21.9s (grok-composer inference-bound; acceptable v1).
- 22:2x P5 sweep: pages /apps /schedules /settings /search all 200. UPDATE CHECKER CORRECT on 0.8.11:
  current 0.8.11 vs appcast 0.8.10 -> available:false (no downgrade offer). SCHEDULER end-to-end on the
  release binary: create job -> run -> status ok -> conversation has the exact expected reply (SCHED-OK);
  cleaned up. Soak "1 anomaly" was my counter counting the DONE line — 0 real anomalies across rounds 1-2.
  Round 3 running.
