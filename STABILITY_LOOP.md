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
- [ ] P2. Land work on master: merge feat/grok-image-search (12 commits, user-tested) + PR #13
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
