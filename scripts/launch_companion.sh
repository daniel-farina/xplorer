#!/bin/sh
# Start the Grok companion sidebar server (chat UI on :9345).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "$ROOT/sdk/grok_companion_server.py" "$@"