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
- [ ] P3. Cross-platform compile+test: macOS (done continuously), Linux droplet (restore snapshot
      233520945), Windows NUC (must first fix the tree state — vtgh.h ANCHOR failure = revert not
      reaching pristine; investigate `git status` clean but content differs → try `git checkout -f
      origin/main -- .` or re-fetch).
- [ ] P4. DRAFT release 0.8.11: 3-OS artifacts, gh release create --draft. NO publish.
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
