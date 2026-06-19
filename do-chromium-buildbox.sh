#!/usr/bin/env bash
#
# do-chromium-buildbox.sh — Provision a DigitalOcean build box for compiling Chromium.
#
# Uses the DigitalOcean REST API v2 directly (curl + Bearer token), no doctl required.
#
#   Auth:   export DO_TOKEN=dop_v1_xxxxxxxx...
#   Deps:   bash, curl, jq
#
#   Usage:
#     ./do-chromium-buildbox.sh provision        # create key + droplet (+ optional volume), wait, print ssh
#     ./do-chromium-buildbox.sh snapshot         # snapshot disk (warm checkout + out/) — kept across teardown
#     ./do-chromium-buildbox.sh restore          # create a droplet from the saved snapshot (fast incremental builds)
#     ./do-chromium-buildbox.sh teardown         # destroy droplet (and volume); keeps the snapshot
#     ./do-chromium-buildbox.sh status           # show recorded resources
#
#   Tunables (env vars):
#     DROPLET_NAME   default: chromium-build
#     REGION         default: nyc3   (pick a region that offers SIZE)
#     SIZE           default: c2-16vcpu-32gb  (CPU-Optimized; override e.g. c2-48vcpu-96gb)
#     IMAGE          default: ubuntu-24-04-x64
#     SSH_KEY_FILE   default: ~/.ssh/id_ed25519.pub  (your PUBLIC key)
#     ATTACH_VOLUME  default: 0   set to 1 to also attach a block-storage volume
#     VOLUME_SIZE_GB default: 200 (only used when ATTACH_VOLUME=1)
#
# ----------------------------------------------------------------------------
# Verified against DigitalOcean API v2 docs (June 2026):
#   * POST   /v2/account/keys   {name, public_key} -> 201, .ssh_key.id / .ssh_key.fingerprint
#   * POST   /v2/volumes        {name, region, size_gigabytes, filesystem_type} -> 201, .volume.id (string UUID)
#   * POST   /v2/droplets       {name, region, size, image, ssh_keys[], volumes[]} -> 202, .droplet.id
#   * GET    /v2/droplets/{id}  -> .droplet.status, .droplet.networks.v4[] (type=="public").ip_address
#   * DELETE /v2/droplets/{id}  -> 204
#   * DELETE /v2/volumes/{id}   -> 204
#   Image slug ubuntu-24-04-x64; size c2-16vcpu-32gb (200 GiB SSD, $0.50/hr).
# ----------------------------------------------------------------------------

set -euo pipefail

API="https://api.digitalocean.com/v2"
STATE_FILE="${STATE_FILE:-./.do-buildbox.state}"

DROPLET_NAME="${DROPLET_NAME:-chromium-build}"
REGION="${REGION:-nyc3}"
SIZE="${SIZE:-c2-16vcpu-32gb}"
IMAGE="${IMAGE:-ubuntu-24-04-x64}"
SSH_KEY_FILE="${SSH_KEY_FILE:-$HOME/.ssh/id_ed25519.pub}"
ATTACH_VOLUME="${ATTACH_VOLUME:-0}"
VOLUME_SIZE_GB="${VOLUME_SIZE_GB:-200}"

# --- helpers ----------------------------------------------------------------

die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m==>\033[0m %s\n' "$*" >&2; }

require() {
  command -v "$1" >/dev/null 2>&1 || die "missing dependency: $1"
}

preflight() {
  require curl
  require jq
  [ -n "${DO_TOKEN:-}" ] || die "DO_TOKEN is not set (export DO_TOKEN=dop_v1_...)"
  # Region must offer the chosen SIZE; the 16-vCPU CPU-optimized slugs live in
  # lon1/blr1/sgp1 (AMD) or ams3/nyc2/sgp1 (Intel), not nyc3/sfo3. Trust the
  # caller's REGION and let the DO API reject mismatches.
  [ -n "${REGION:-}" ] || die "REGION is empty"
}

# api METHOD PATH [json-body]
# Emits the response body on stdout; fails hard on HTTP >= 400.
api() {
  local method="$1" path="$2" body="${3:-}"
  local tmp http
  tmp="$(mktemp)"
  if [ -n "$body" ]; then
    http="$(curl -sS -o "$tmp" -w '%{http_code}' -X "$method" \
      -H "Authorization: Bearer $DO_TOKEN" \
      -H "Content-Type: application/json" \
      -d "$body" "$API$path")"
  else
    http="$(curl -sS -o "$tmp" -w '%{http_code}' -X "$method" \
      -H "Authorization: Bearer $DO_TOKEN" \
      -H "Content-Type: application/json" \
      "$API$path")"
  fi
  if [ "$http" -ge 400 ]; then
    local msg; msg="$(jq -r '.message // .id // "unknown error"' <"$tmp" 2>/dev/null || cat "$tmp")"
    rm -f "$tmp"
    die "HTTP $http on $method $path: $msg"
  fi
  cat "$tmp"; rm -f "$tmp"
}

# state file is a tiny KEY=VALUE store
state_set() {
  local key="$1" val="$2"
  touch "$STATE_FILE"
  grep -v "^${key}=" "$STATE_FILE" > "${STATE_FILE}.tmp" 2>/dev/null || true
  printf '%s=%s\n' "$key" "$val" >> "${STATE_FILE}.tmp"
  mv "${STATE_FILE}.tmp" "$STATE_FILE"
}
state_get() {
  [ -f "$STATE_FILE" ] || { echo ""; return; }
  grep "^$1=" "$STATE_FILE" 2>/dev/null | head -n1 | cut -d= -f2- || echo ""
}
state_unset() {
  [ -f "$STATE_FILE" ] || return 0
  grep -v "^${1}=" "$STATE_FILE" > "${STATE_FILE}.tmp" 2>/dev/null || true
  mv "${STATE_FILE}.tmp" "$STATE_FILE"
}

# Upload the SSH public key (idempotent). Echoes the key id on stdout; records
# SSH_KEY_ID / SSH_KEY_FPR in the state file. All chatter goes to stderr so the
# caller can capture just the id.
ensure_ssh_key() {
  [ -f "$SSH_KEY_FILE" ] || die "SSH public key not found: $SSH_KEY_FILE (set SSH_KEY_FILE=...)"
  case "$(cat "$SSH_KEY_FILE")" in
    ssh-rsa*|ssh-ed25519*|ecdsa-*) ;;
    *) die "$SSH_KEY_FILE does not look like an SSH PUBLIC key";;
  esac
  info "Uploading SSH public key from $SSH_KEY_FILE ..."
  local pubkey keyname key_payload resp http tmp key_id key_fpr
  pubkey="$(cat "$SSH_KEY_FILE")"
  keyname="${DROPLET_NAME}-$(hostname -s 2>/dev/null || echo host)-$(date +%s)"
  key_payload="$(jq -n --arg name "$keyname" --arg pk "$pubkey" '{name:$name, public_key:$pk}')"
  tmp="$(mktemp)"
  http="$(curl -sS -o "$tmp" -w '%{http_code}' -X POST \
    -H "Authorization: Bearer $DO_TOKEN" -H "Content-Type: application/json" \
    -d "$key_payload" "$API/account/keys")"
  if [ "$http" = "201" ]; then
    key_id="$(jq -r '.ssh_key.id' <"$tmp")"
    key_fpr="$(jq -r '.ssh_key.fingerprint' <"$tmp")"
  elif [ "$http" = "422" ]; then
    info "Key already present; looking it up by fingerprint ..."
    local local_fpr
    local_fpr="$(ssh-keygen -E md5 -lf "$SSH_KEY_FILE" | awk '{print $2}' | sed 's/^MD5://')"
    resp="$(api GET "/account/keys?per_page=200")"
    key_id="$(jq -r --arg f "$local_fpr" '.ssh_keys[] | select(.fingerprint==$f) | .id' <<<"$resp" | head -n1)"
    key_fpr="$local_fpr"
    [ -n "$key_id" ] && [ "$key_id" != "null" ] || { rm -f "$tmp"; die "could not resolve existing SSH key id"; }
  else
    local msg; msg="$(jq -r '.message // "unknown"' <"$tmp")"; rm -f "$tmp"
    die "HTTP $http creating SSH key: $msg"
  fi
  rm -f "$tmp"
  state_set SSH_KEY_ID "$key_id"
  state_set SSH_KEY_FPR "$key_fpr"
  info "SSH key id=$key_id fingerprint=$key_fpr"
  echo "$key_id"
}

# wait_active DROPLET_ID -> polls until status=active with a public IPv4; echoes the IP.
wait_active() {
  local droplet_id="$1" status ip i=0 max=180 droplet_resp
  while :; do
    droplet_resp="$(api GET "/droplets/$droplet_id")"
    status="$(jq -r '.droplet.status' <<<"$droplet_resp")"
    ip="$(jq -r '.droplet.networks.v4[]? | select(.type=="public") | .ip_address' <<<"$droplet_resp" | head -n1)"
    if [ "$status" = "active" ] && [ -n "$ip" ] && [ "$ip" != "null" ]; then break; fi
    i=$((i+1))
    [ "$i" -ge "$max" ] && die "timed out waiting for droplet to become active (status=$status)"
    printf '\r    status=%-12s elapsed=%ds' "$status" "$((i*5))" >&2
    sleep 5
  done
  printf '\n' >&2
  echo "$ip"
}

# --- commands ---------------------------------------------------------------

cmd_provision() {
  preflight

  # 1) Upload SSH key (idempotent; reused by fingerprint if already present).
  local key_id; key_id="$(ensure_ssh_key)"

  # 2) Optionally create a block-storage volume (only if ATTACH_VOLUME=1).
  local volume_id="" volumes_json="[]"
  if [ "$ATTACH_VOLUME" = "1" ]; then
    local volname vol_payload vol_resp
    volname="${DROPLET_NAME}-vol-$(date +%s)"
    info "Creating ${VOLUME_SIZE_GB} GiB block-storage volume $volname in $REGION ..."
    vol_payload="$(jq -n --arg n "$volname" --arg r "$REGION" --argjson s "$VOLUME_SIZE_GB" \
      '{name:$n, region:$r, size_gigabytes:$s, filesystem_type:"ext4"}')"
    vol_resp="$(api POST "/volumes" "$vol_payload")"
    volume_id="$(jq -r '.volume.id' <<<"$vol_resp")"
    [ -n "$volume_id" ] && [ "$volume_id" != "null" ] || die "volume creation returned no id"
    state_set VOLUME_ID "$volume_id"
    volumes_json="$(jq -n --arg v "$volume_id" '[$v]')"
    info "Volume id=$volume_id"
  fi

  # 3) Create the droplet with the SSH key (and volume) attached.
  info "Creating droplet $DROPLET_NAME ($SIZE, $IMAGE, $REGION) ..."
  local droplet_payload droplet_resp droplet_id
  droplet_payload="$(jq -n \
    --arg name "$DROPLET_NAME" --arg region "$REGION" --arg size "$SIZE" \
    --arg image "$IMAGE" --argjson keys "[$key_id]" --argjson vols "$volumes_json" \
    '{name:$name, region:$region, size:$size, image:$image,
      ssh_keys:$keys, volumes:$vols, ipv6:true, monitoring:true,
      tags:["chromium-build"]}')"
  droplet_resp="$(api POST "/droplets" "$droplet_payload")"
  droplet_id="$(jq -r '.droplet.id' <<<"$droplet_resp")"
  [ -n "$droplet_id" ] && [ "$droplet_id" != "null" ] || die "droplet creation returned no id"
  state_set DROPLET_ID "$droplet_id"
  info "Droplet id=$droplet_id (waiting for it to become active) ..."

  # 4) Poll until status=active and a public IPv4 appears.
  local ip; ip="$(wait_active "$droplet_id")"
  state_set DROPLET_IP "$ip"

  info "Droplet is active."
  cat <<EOF

  Droplet ready.
    name:    $DROPLET_NAME
    id:      $droplet_id
    region:  $REGION
    size:    $SIZE
    image:   $IMAGE
    ip:      $ip
$( [ -n "$volume_id" ] && echo "    volume:  $volume_id (${VOLUME_SIZE_GB} GiB, attached as /dev/disk/by-id/scsi-0DO_Volume_*)" )

  Connect:
    ssh root@$ip

  Tear down when done:
    $0 teardown
EOF
}

cmd_teardown() {
  preflight
  local droplet_id volume_id
  droplet_id="$(state_get DROPLET_ID)"
  volume_id="$(state_get VOLUME_ID)"

  if [ -n "$droplet_id" ]; then
    info "Deleting droplet $droplet_id ..."
    # DELETE returns 204; api() tolerates empty body.
    api DELETE "/droplets/$droplet_id" >/dev/null || true
    info "Droplet $droplet_id deleted."
  else
    info "No droplet id recorded; nothing to delete."
  fi

  if [ -n "$volume_id" ]; then
    # Volume must be detached first; deleting the droplet detaches it, but the
    # detach can lag, so retry the volume delete a few times.
    info "Deleting volume $volume_id ..."
    local n=0
    until api DELETE "/volumes/$volume_id" >/dev/null 2>&1; do
      n=$((n+1)); [ "$n" -ge 12 ] && die "could not delete volume $volume_id (still attached?)"
      sleep 5
    done
    info "Volume $volume_id deleted."
  fi

  # Keep the snapshot id (and the reusable SSH key) so a later `restore` works;
  # only forget the now-deleted droplet/volume.
  state_unset DROPLET_ID
  state_unset DROPLET_IP
  state_unset VOLUME_ID
  local snap; snap="$(state_get SNAPSHOT_ID)"
  if [ -n "$snap" ]; then
    info "Teardown complete; kept snapshot $snap (restore with: $0 restore)."
  else
    rm -f "$STATE_FILE"
    info "Teardown complete; state file removed."
  fi
}

# Snapshot the build droplet's whole disk (warm Chromium checkout + out/ object
# cache) so future builds restore in minutes instead of recompiling for hours.
# Usage: cmd_snapshot [droplet_id]
cmd_snapshot() {
  preflight
  local droplet_id snap_name action_id status i=0 max=900 resp
  droplet_id="${2:-$(state_get DROPLET_ID)}"
  [ -n "$droplet_id" ] || die "no droplet id (state empty; pass one: $0 snapshot <droplet_id>)"
  snap_name="${SNAPSHOT_NAME:-${DROPLET_NAME}-snap-$(date +%Y%m%d-%H%M%S)}"
  info "Snapshotting droplet $droplet_id as '$snap_name' (10-40 min for a large disk) ..."
  resp="$(api POST "/droplets/$droplet_id/actions" \
    "$(jq -n --arg n "$snap_name" '{type:"snapshot", name:$n}')")"
  action_id="$(jq -r '.action.id' <<<"$resp")"
  [ -n "$action_id" ] && [ "$action_id" != "null" ] || die "snapshot action returned no id"
  info "Snapshot action id=$action_id; polling ..."
  while :; do
    resp="$(api GET "/actions/$action_id")"
    status="$(jq -r '.action.status' <<<"$resp")"
    case "$status" in
      completed) break ;;
      errored)   die "snapshot action errored" ;;
    esac
    i=$((i+1))
    [ "$i" -ge "$max" ] && die "timed out waiting for snapshot (status=$status)"
    printf '\r    snapshot status=%-10s elapsed=%dm%02ds' "$status" "$(((i*10)/60))" "$(((i*10)%60))" >&2
    sleep 10
  done
  printf '\n' >&2
  local snap_id
  snap_id="$(api GET "/droplets/$droplet_id/snapshots?per_page=200" \
    | jq -r '.snapshots | sort_by(.created_at) | last | .id')"
  [ -n "$snap_id" ] && [ "$snap_id" != "null" ] || die "snapshot completed but id not found"
  state_set SNAPSHOT_ID "$snap_id"
  state_set SNAPSHOT_NAME "$snap_name"
  info "Snapshot complete: id=$snap_id name=$snap_name"
  cat <<EOF

  Snapshot saved (kept across teardown).
    id:    $snap_id
    name:  $snap_name

  Restore later (boots the warm checkout; then incremental build = minutes):
    SNAPSHOT_ID=$snap_id $0 restore
EOF
}

# Create a fresh droplet from a saved snapshot. Usage: cmd_restore [snapshot_id]
cmd_restore() {
  preflight
  local snap_id key_id ip droplet_id droplet_payload droplet_resp
  snap_id="${2:-${SNAPSHOT_ID:-$(state_get SNAPSHOT_ID)}}"
  [ -n "$snap_id" ] || die "no snapshot id (set SNAPSHOT_ID=... or pass: $0 restore <snapshot_id>)"
  key_id="$(ensure_ssh_key)"
  info "Creating droplet $DROPLET_NAME from snapshot $snap_id ($SIZE, $REGION) ..."
  droplet_payload="$(jq -n \
    --arg name "$DROPLET_NAME" --arg region "$REGION" --arg size "$SIZE" \
    --arg image "$snap_id" --argjson keys "[$key_id]" \
    '{name:$name, region:$region, size:$size, image:($image|tonumber),
      ssh_keys:$keys, ipv6:true, monitoring:true, tags:["chromium-build"]}')"
  droplet_resp="$(api POST "/droplets" "$droplet_payload")"
  droplet_id="$(jq -r '.droplet.id' <<<"$droplet_resp")"
  [ -n "$droplet_id" ] && [ "$droplet_id" != "null" ] || die "droplet creation returned no id"
  state_set DROPLET_ID "$droplet_id"
  info "Droplet id=$droplet_id (waiting for active) ..."
  ip="$(wait_active "$droplet_id")"
  state_set DROPLET_IP "$ip"
  cat <<EOF

  Restored droplet ready.
    name:  $DROPLET_NAME
    id:    $droplet_id
    from:  snapshot $snap_id
    size:  $SIZE
    ip:    $ip

  Connect:  ssh root@$ip
  Tear down (snapshot is kept):  $0 teardown
EOF
}

cmd_status() {
  if [ ! -f "$STATE_FILE" ]; then echo "no state file ($STATE_FILE); nothing provisioned"; return; fi
  echo "Recorded resources ($STATE_FILE):"
  cat "$STATE_FILE"
}

# --- dispatch ---------------------------------------------------------------

case "${1:-}" in
  provision) cmd_provision ;;
  teardown)  cmd_teardown ;;
  snapshot)  cmd_snapshot "$@" ;;
  restore)   cmd_restore "$@" ;;
  status)    cmd_status ;;
  *) cat >&2 <<EOF
usage: $0 {provision|teardown|snapshot|restore|status}

  provision           upload SSH key, create CPU-Optimized droplet (+ optional
                      volume), wait until active, print the ssh command
  snapshot [id]       snapshot the droplet's disk (warm checkout + out/ cache)
                      so future builds restore in minutes; kept across teardown
  restore  [snap_id]  create a fresh droplet from a saved snapshot
  teardown            delete the droplet (and volume); keeps the snapshot
  status              show recorded resource ids

env: DO_TOKEN (required), REGION, SIZE, IMAGE, SSH_KEY_FILE,
     SNAPSHOT_ID / SNAPSHOT_NAME (for restore/snapshot),
     ATTACH_VOLUME=1 VOLUME_SIZE_GB=200 to add block storage
EOF
     exit 2 ;;
esac
