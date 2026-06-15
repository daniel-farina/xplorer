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
| `assets/logo.png` | the **X** logo — hero wordmark + footer + favicon (square) |
| `assets/screenshot-hero.png` | big hero screenshot (16:9) |
| `assets/screenshot-build.png` | "Build apps by describing them" (builder + live preview) |
| `assets/screenshot-grokit.png` | "The Grok it button" (FAB menu on a page) |
| `assets/screenshot-toolbar.png` | "All of Grok in one toolbar" |

> `assets/logo.png` currently holds a temporary placeholder icon — replace it
> with the real logo when ready.

## Deploying (GitHub Pages)

A workflow at `.github/workflows/pages.yml` publishes everything in `site/`
on every push to `master`. To enable it once the repo is public:

1. Repo **Settings → Pages → Build and deployment → Source: GitHub Actions**.
2. Push to `master` (or run the workflow manually via *Actions → Deploy landing
   page → Run workflow*).
