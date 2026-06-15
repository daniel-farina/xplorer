# Xplorer landing page

Static landing page for **Xplorer** — the open-source, Grok-native browser.
Plain HTML/CSS, no build step.

## Local preview

Just open `index.html` in a browser, or serve it:

```sh
cd site && python3 -m http.server 8080
# → http://localhost:8080
```

## Assets to add

Drop your real images into `assets/` using these exact names and they'll appear
automatically (until then, styled placeholders show):

| File | Where it shows |
|------|----------------|
| `assets/screenshot-hero.png` | big hero screenshot (16:9) |
| `assets/screenshot-build.png` | "Build apps by describing them" (builder + live preview) |
| `assets/screenshot-grokit.png` | "The Grok it button" (FAB menu on a page) |
| `assets/screenshot-toolbar.png` | "All of Grok in one toolbar" |
| `assets/screenshot-newtab-1.png` | Customize — **Image #1** (new tab, custom light bg) |
| `assets/screenshot-newtab-2.png` | Customize — **Image #2** (new tab, custom dark bg) |
| `assets/screenshot-newtab-3.png` | Customize — **Image #3** (background picker panel) |
| `assets/screenshot-newtab-4.png` | Customize — **Image #4** (drag & drop) |

Until you drop these in, styled placeholders show in their place.

## Logo & icons

The logo is a clean **X** mark in `assets/logo.svg` (theme-aware — dark on light
UI, light on dark). The full icon set is generated from it:

- `icon-16/32/48.png` + `icon-16/32-dark.png` — adaptive browser-tab favicons
- `apple-touch-icon.png` (180) — iOS home screen
- `icon-192/512.png` + `icon-maskable-192/512.png` — PWA / Android (`site.webmanifest`)

To regenerate after editing `logo.svg`, re-run the headless-Chrome render
(see commit history) — no design tool needed.

## Theme & the dark-mode transition

The site is **light by default** (clean, Apple-style) with a **dark mode** behind
the sun/moon toggle (saved to `localStorage`). Backdrops:

- Light: `assets/lightmode.jpg`
- Dark: `assets/darkmode.jpg` — the **last frame** of the transition video

Toggling **plays a video**: `assets/anim_to_dark.mp4` (day→night) on the way to
dark, `assets/anim_to_light.mp4` (night→day) on the way back, then settles on the
matching static image. Refreshing loads the current mode's static image with no
animation. Source clip: `assets/anim_dark_mode.mp4` (re-encode the two directional
clips from it with ffmpeg if you swap it). All colors are CSS variables in
`:root` / `:root[data-theme="dark"]` at the top of `styles.css`.

## Deploying (GitHub Pages)

A workflow at `.github/workflows/pages.yml` publishes everything in `site/`
on every push to `master`. To enable it once the repo is public:

1. Repo **Settings → Pages → Build and deployment → Source: GitHub Actions**.
2. Push to `master` (or run the workflow manually via *Actions → Deploy landing
   page → Run workflow*).
