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
| `assets/logo.png` | nav + footer + favicon (square, ~256px+) |
| `assets/screenshot-hero.png` | big hero screenshot (16:9) |
| `assets/screenshot-sidebar.png` | "Grok in the sidebar" |
| `assets/screenshot-command.png` | "Command bar" |
| `assets/screenshot-build.png` | "Grok Build, inside the browser" |
| `assets/screenshot-agents.png` | "Agents driving the web" |

> `assets/logo.png` currently holds a temporary placeholder icon — replace it
> with the real logo when ready.

## Deploying (GitHub Pages)

A workflow at `.github/workflows/pages.yml` publishes everything in `site/`
on every push to `master`. To enable it once the repo is public:

1. Repo **Settings → Pages → Build and deployment → Source: GitHub Actions**.
2. Push to `master` (or run the workflow manually via *Actions → Deploy landing
   page → Run workflow*).
