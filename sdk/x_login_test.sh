#!/usr/bin/env bash
# x_login_test.sh — prove REAL X API access via the xurl OAuth bridge.
#
# Run it, log in with your X account in the browser that opens, and it prints your
# real X profile + a real live search. This is the same OAuth path Xplor's `xapi`
# MCP server uses — once this works, Xplor's X features switch from mock to live.
#
# USAGE
#   # If you have an X dev app (developer.x.com, OAuth 2.0, redirect
#   #   http://localhost:8080/callback), export its creds first:
#   export CLIENT_ID=<your X app OAuth2 Client ID>
#   export CLIENT_SECRET=<your X app OAuth2 Client Secret>
#   bash sdk/x_login_test.sh
#
#   # (If you've already run `xurl auth` before, the active app in ~/.xurl is used
#   #  and the exports are optional.)
set -uo pipefail
XURL=(npx -y @xdevplatform/xurl)

echo "=== xurl bridge ==="
"${XURL[@]}" --version 2>&1 | head -1 || true
if [ -z "${CLIENT_ID:-}" ] && [ ! -f "$HOME/.xurl" ]; then
  echo "NOTE: no CLIENT_ID env and no ~/.xurl — set CLIENT_ID/CLIENT_SECRET (see header) first."
fi
echo

echo "=== 1) OAuth login — a browser window will open; log in with your X account ==="
if ! "${XURL[@]}" auth oauth2; then
  echo "!! auth failed. Check: app has OAuth 2.0 enabled, redirect http://localhost:8080/callback"
  echo "   registered, app in a Production environment, and CLIENT_ID/CLIENT_SECRET correct."
  exit 1
fi
echo

echo "=== 2) Who am I? (your real X account) ==="
"${XURL[@]}" /2/users/me
echo

echo "=== 3) Real live X search: recent posts about 'ai agents' ==="
"${XURL[@]}" "/2/tweets/search/recent?query=ai%20agents&max_results=5&tweet.fields=public_metrics,created_at"
echo

echo "=== done — if you see your handle + real posts above, live X access works."
echo "    Paste me the output and I'll flip Xplor's xapi server on (mock -> live). ==="
