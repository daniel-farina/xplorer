#!/usr/bin/env python3
"""Xplor least-privilege OAuth2 (PKCE) for the X API.

Replaces xurl's broad, hardcoded scope request (which includes DM + email + write
and makes the X consent screen fail on most apps). This requests ONLY the scopes
Xplor's features need — all grantable on a basic Read+write app — so the OAuth
succeeds, then PROBES the account's actual access so features can adapt to the
user's tier (capability detection).

Usage:
    export CLIENT_ID=<your X app OAuth2 Client ID>
    export CLIENT_SECRET=<your X app OAuth2 Client Secret>   # confidential client
    python3 sdk/x_oauth.py

On success the token (access + refresh + expiry + scope) is cached at
~/.xplorer/x_token.json and the script prints a capability summary. The bridge
(x_oauth_mcp.py) reuses the cached token + refreshes it.
"""
import os
import sys
import json
import time
import base64
import hashlib
import secrets
import threading
import urllib.parse
import urllib.request
import urllib.error
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer

# Least-privilege: read + bookmarks + post. NO dm.*, NO users.email, NO block/mute
# — those are what make xurl's consent fail, and Xplor's features never use them.
SCOPES = ("tweet.read users.read offline.access bookmark.read bookmark.write "
          "tweet.write like.read follows.read list.read")
REDIRECT = "http://localhost:8080/callback"
AUTH_URL = "https://x.com/i/oauth2/authorize"
TOKEN_URL = "https://api.x.com/2/oauth2/token"
TOKEN_FILE = os.path.expanduser("~/.xplorer/x_token.json")


def _b64url(raw):
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode()


def build_auth_url(client_id, verifier, state):
    challenge = _b64url(hashlib.sha256(verifier.encode()).digest())
    q = urllib.parse.urlencode({
        "response_type": "code", "client_id": client_id, "redirect_uri": REDIRECT,
        "scope": SCOPES, "state": state,
        "code_challenge": challenge, "code_challenge_method": "S256",
    })
    return AUTH_URL + "?" + q


def _api(path, token):
    req = urllib.request.Request("https://api.x.com" + path,
                                 headers={"Authorization": "Bearer " + token})
    try:
        return json.load(urllib.request.urlopen(req, timeout=20)), 200
    except urllib.error.HTTPError as e:
        return None, e.code
    except Exception:
        return None, 0


def main():
    cid = os.environ.get("CLIENT_ID")
    csec = os.environ.get("CLIENT_SECRET")
    if not cid:
        print("Set CLIENT_ID (and CLIENT_SECRET) from your X app — see the header.")
        sys.exit(1)

    verifier = _b64url(secrets.token_bytes(64))
    state = secrets.token_hex(16)
    url = build_auth_url(cid, verifier, state)

    box = {}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            d = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            box["code"] = d.get("code", [None])[0]
            box["state"] = d.get("state", [None])[0]
            box["error"] = d.get("error", [None])[0]
            ok = bool(box.get("code"))
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            msg = ("Xplor: X login complete — you can close this tab." if ok
                   else "Xplor: X login failed — " + str(box.get("error")))
            self.wfile.write(("<html><body style='font-family:sans-serif;padding:3rem'>"
                              + msg + "</body></html>").encode())

        def log_message(self, *a):
            pass

    srv = HTTPServer(("127.0.0.1", 8080), Handler)
    print("Opening the browser to log in to X (least-privilege scopes)…")
    print("If it doesn't open, visit:\n" + url + "\n")
    threading.Thread(target=lambda: webbrowser.open(url), daemon=True).start()
    srv.handle_request()  # serve exactly the one OAuth callback

    if box.get("error") or not box.get("code"):
        print("OAuth failed:", box.get("error") or "no authorization code returned")
        sys.exit(1)
    if box.get("state") != state:
        print("state mismatch — aborting (possible CSRF)")
        sys.exit(1)

    body = urllib.parse.urlencode({
        "grant_type": "authorization_code", "code": box["code"],
        "redirect_uri": REDIRECT, "code_verifier": verifier, "client_id": cid,
    }).encode()
    req = urllib.request.Request(
        TOKEN_URL, data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"})
    if csec:  # confidential client -> HTTP Basic with the client secret
        req.add_header("Authorization", "Basic " +
                       base64.b64encode(f"{cid}:{csec}".encode()).decode())
    try:
        tok = json.load(urllib.request.urlopen(req, timeout=20))
    except urllib.error.HTTPError as e:
        print("token exchange failed:", e.read().decode()[:400])
        sys.exit(1)

    tok["obtained_at"] = int(time.time())
    os.makedirs(os.path.dirname(TOKEN_FILE), exist_ok=True)
    with open(TOKEN_FILE, "w") as f:
        f.write(json.dumps(tok, indent=2))
    print("\n✓ OAuth succeeded. Token cached at", TOKEN_FILE)
    print("  granted scopes:", tok.get("scope", "?"))

    # --- capability detection: what can this account actually do? ---
    at = tok["access_token"]
    me, c1 = _api("/2/users/me", at)
    handle = (me or {}).get("data", {}).get("username", "?")
    print("\nCapability detection:")
    print("  account:        @%s  (/2/users/me -> %s)" % (handle, c1))
    _, c2 = _api("/2/tweets/search/recent?query=ai&max_results=10", at)
    search = ("AVAILABLE" if c2 == 200 else
              "NOT on this tier (needs Basic+ API)" if c2 == 403 else "code %s" % c2)
    print("  post search:    %s  (/2/tweets/search/recent -> %s)" % (search, c2))
    _, c3 = _api("/2/users/me/bookmarks?max_results=1", at) if me else (None, 0)
    bookmarks = ("AVAILABLE" if c3 == 200 else
                 "needs Basic+ / scope" if c3 in (403, 401) else "code %s" % c3)
    print("  bookmarks:      %s  (/2/users/:id/bookmarks -> %s)" % (bookmarks, c3))
    print("\n-> Xplor features adapt to this: real data where AVAILABLE, the mock "
          "fallback otherwise. Paste this summary back and I'll wire it in.")


if __name__ == "__main__":
    main()
