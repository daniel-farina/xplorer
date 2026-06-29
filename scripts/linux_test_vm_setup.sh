#!/usr/bin/env bash
# Bootstrap a local Ubuntu VM (Multipass) for manually testing the Linux tarball.
# Run on macOS after: brew install --cask multipass
#
#   ./scripts/linux_test_vm_setup.sh [path-to-Xplorer-linux-x64.tar.gz]
#
# Creates VM "xplorer-linux-test", stages the tarball, installs Chrome runtime
# deps + a lightweight XFCE desktop + TigerVNC, and prints how to connect.
set -euo pipefail

VM_NAME="${VM_NAME:-xplorer-linux-test}"
TARBALL="${1:-$(cd "$(dirname "$0")/.." && pwd)/dist-linux/Xplorer-linux-x64.tar.gz}"
VNC_PORT="${VNC_PORT:-5901}"
VNC_DISPLAY="${VNC_DISPLAY:-:1}"

command -v multipass >/dev/null 2>&1 || {
  echo "multipass not found. Install with: brew install --cask multipass" >&2
  exit 1
}
[ -f "$TARBALL" ] || { echo "tarball not found: $TARBALL" >&2; exit 1; }

info() { printf '\033[36m==>\033[0m %s\n' "$*"; }

if multipass info "$VM_NAME" >/dev/null 2>&1; then
  info "VM $VM_NAME already exists — reusing it"
else
  info "Launching Ubuntu 24.04 VM ($VM_NAME) ..."
  multipass launch 24.04 \
    --name "$VM_NAME" \
    --cpus 4 \
    --memory 6G \
    --disk 30G
fi

info "Transferring $(basename "$TARBALL") into the VM ..."
multipass transfer "$TARBALL" "${VM_NAME}:/home/ubuntu/Xplorer-linux-x64.tar.gz"

info "Installing desktop + browser deps inside the VM (first run takes a few minutes) ..."
multipass exec "$VM_NAME" -- sudo bash -s <<'REMOTE'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update -y
apt-get install -y \
  xfce4 xfce4-terminal tigervnc-standalone-server dbus-x11 \
  libgtk-3-0 libnss3 libxss1 libasound2t64 libgbm1 libxkbcommon0 \
  libatk-bridge2.0-0 libatk1.0-0 libcups2 libdrm2 libxcomposite1 \
  libxdamage1 libxfixes3 libxrandr2 libpango-1.0-0 libcairo2 \
  fonts-liberation xdg-utils ca-certificates curl tar

mkdir -p /home/ubuntu/xplorer-test
tar -xzf /home/ubuntu/Xplorer-linux-x64.tar.gz -C /home/ubuntu/xplorer-test --strip-components=1
chown -R ubuntu:ubuntu /home/ubuntu/xplorer-test

# TigerVNC password: "xplorer" (change after first login if you like)
mkdir -p /home/ubuntu/.vnc
printf 'xplorer\nxplorer\nn\n' | vncpasswd /home/ubuntu/.vnc/passwd
chown -R ubuntu:ubuntu /home/ubuntu/.vnc
chmod 600 /home/ubuntu/.vnc/passwd

cat > /home/ubuntu/.vnc/xstartup <<'XSTART'
#!/bin/sh
unset SESSION_MANAGER
unset DBUS_SESSION_BUS_ADDRESS
exec startxfce4
XSTART
chmod +x /home/ubuntu/.vnc/xstartup
chown ubuntu:ubuntu /home/ubuntu/.vnc/xstartup

cat > /home/ubuntu/start-xplorer.sh <<'SCRIPT'
#!/bin/bash
cd ~/xplorer-test
export DISPLAY=${DISPLAY:-:1}
./xplorer --no-sandbox --user-data-dir="$HOME/.xplorer-test-profile" "$@" &
SCRIPT
chmod +x /home/ubuntu/start-xplorer.sh
chown ubuntu:ubuntu /home/ubuntu/start-xplorer.sh

# systemd user service for VNC (runs as ubuntu)
cat > /etc/systemd/system/xplorer-vnc.service <<'UNIT'
[Unit]
Description=Xplorer test VNC desktop
After=network.target

[Service]
Type=simple
User=ubuntu
Environment=HOME=/home/ubuntu
ExecStart=/usr/bin/vncserver :1 -geometry 1440x900 -depth 24 -localhost no -SecurityTypes VncAuth
ExecStop=/usr/bin/vncserver -kill :1
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNIT

systemctl daemon-reload
systemctl enable --now xplorer-vnc.service
REMOTE

VM_IP="$(multipass info "$VM_NAME" --format csv | tail -n1 | cut -d, -f3)"
cat <<EOF

============================================================
  Xplorer Linux test VM is ready
============================================================

  VM name:     $VM_NAME
  VM IP:       $VM_IP

  VNC desktop (open in Screen Sharing / any VNC client):
    vnc://$VM_IP:$VNC_PORT
    password: xplorer

  SSH shell:
    multipass shell $VM_NAME

  Inside the VM:
    cd ~/xplorer-test && ./xplorer --no-sandbox
    # or from the VNC desktop terminal:
    ~/start-xplorer.sh

  Tarball staged at:
    /home/ubuntu/xplorer-test/

  Tear down when done:
    multipass delete $VM_NAME --purge
============================================================
EOF