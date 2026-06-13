#!/usr/bin/env bash
# Smoke test native Grok companion on AgentGateway (default :9334).
# Requires XBrowser running. Exit non-zero on failure.
set -euo pipefail

PORT="${XBROWSER_GATEWAY_PORT:-9334}"
BASE="http://127.0.0.1:${PORT}"
STREAM_TIMEOUT="${SMOKE_STREAM_TIMEOUT:-30}"

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "OK: $*"; }

curl -sf "${BASE}/api/status" >/tmp/xb_status.json \
  || fail "api/status unreachable on ${BASE}"
python3 -c "
import json
d = json.load(open('/tmp/xb_status.json'))
assert d.get('native') and d.get('ok'), d
assert d.get('model'), 'missing default model'
print('status model:', d.get('model_label', d.get('model')))
" || fail "api/status payload"

curl -sf "${BASE}/api/theme" >/tmp/xb_theme.json \
  || fail "api/theme unreachable"
python3 -c "
import json
d = json.load(open('/tmp/xb_theme.json'))
assert 'color_scheme' in d, d
print('theme:', d['color_scheme'])
" || fail "api/theme payload"

curl -sf "${BASE}/search" | head -1 | grep -q '<!DOCTYPE html>' \
  || fail "/search HTML"

curl -sf "${BASE}/theme.css" | grep -q 'no purple' \
  || fail "theme.css missing"

curl -sf "${BASE}/api/models" >/tmp/xb_models.json
python3 -c "
import json
d = json.load(open('/tmp/xb_models.json'))
ids = [m['id'] for m in d.get('models', [])]
assert 'Default' not in ids, ids
assert any('composer' in i or 'grok' in i for i in ids), ids
print('models:', len(ids))
" || fail "api/models"

# Streaming search — grab first lines with a hard timeout.
set +e
curl -sfN -m "${STREAM_TIMEOUT}" -X POST -H 'Content-Type: application/json' \
  -d '{"query":"2+2","mode":"web","model":"grok-composer-2.5-fast"}' \
  "${BASE}/api/search/stream" 2>/dev/null | head -5 >/tmp/xb_stream.txt
set -e
grep -qE '"type":"(meta|thought|text)"' /tmp/xb_stream.txt \
  || fail "search stream produced no events"
echo "stream: $(head -1 /tmp/xb_stream.txt | cut -c1-80)..."

# Screenshot API — ensure a normal https tab exists first (NTP/search can fail).
if [[ "${SKIP_SCREENSHOT:-0}" != "1" ]]; then
  TOKEN=$(python3 -c "import json; print(json.load(open('/Users/dan/.aether/gateway.json'))['token'])" 2>/dev/null || true)
  if [[ -n "${TOKEN:-}" ]]; then
    curl -sf -X POST -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
      -d '{"url":"https://example.com"}' "${BASE}/tabs" >/dev/null 2>&1 || true
    sleep 2
  fi
  if curl -sf -m 15 -X POST -H 'Content-Type: application/json' -d '{}' \
      "${BASE}/api/screenshot" >/tmp/xb_shot.json 2>/dev/null; then
    python3 -c "
import json, base64, sys
d = json.load(open('/tmp/xb_shot.json'))
if d.get('error'):
    print('WARN: api/screenshot:', d['error'], file=sys.stderr)
    sys.exit(1)
img = d.get('image', '')
assert len(img) > 100, d
base64.b64decode(img)
print('screenshot bytes:', len(img))
" || echo "WARN: api/screenshot failed (vision upload/paste still works)"
  else
    echo "WARN: api/screenshot timed out (vision upload/paste still works)"
  fi
fi

ok "native Grok smoke test passed on ${BASE}"