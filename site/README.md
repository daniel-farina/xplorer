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
| `assets/logo.png` | the logo — hero (above the wordmark) + footer + favicon (square) |
| `assets/lightmode.jpg` | **light-mode hero backdrop** (landscape, 16:9) — already added |
| `assets/darkmode.jpg` | **dark-mode hero backdrop** (16:9) — _optional, drop in to enable_ |
| `assets/screenshot-hero.png` | big hero screenshot (16:9) |
| `assets/screenshot-build.png` | "Build apps by describing them" (builder + live preview) |
| `assets/screenshot-grokit.png` | "The Grok it button" (FAB menu on a page) |
| `assets/screenshot-toolbar.png` | "All of Grok in one toolbar" |

## Theme

The site is **light by default** (clean, Apple-style) and ships a **dark mode**
behind the sun/moon toggle in the nav (preference saved to `localStorage`).
Light mode uses `assets/lightmode.jpg` as the hero backdrop; dark mode looks for
`assets/darkmode.jpg` — until you add one, dark mode just shows the dark
background. All colors are driven by CSS variables in `:root` /
`:root[data-theme="dark"]` at the top of `styles.css`.

> `assets/logo.png` currently holds a temporary placeholder icon — replace it
> with the real logo when ready.

## Deploying (GitHub Pages)

A workflow at `.github/workflows/pages.yml` publishes everything in `site/`
on every push to `master`. To enable it once the repo is public:

1. Repo **Settings → Pages → Build and deployment → Source: GitHub Actions**.
2. Push to `master` (or run the workflow manually via *Actions → Deploy landing
   page → Run workflow*).
