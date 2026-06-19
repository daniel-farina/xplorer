#!/bin/bash
# Build Ubuntu-friendly installers from the staged Linux tree that
# scripts/package_linux.sh produces ($DIST/Xplorer-linux-<arch>/):
#
#   * .deb       — `sudo dpkg -i` / `sudo apt install ./file.deb`; the format
#                  most Ubuntu/Debian users expect. Installs to /opt/Xplorer
#                  with a /usr/bin/xplorer launcher, a menu entry, and icons.
#                  Runtime Depends are derived with dpkg-shlibdeps so they match
#                  the build platform (e.g. the Ubuntu 24.04 t64 lib names).
#   * .AppImage  — download, `chmod +x`, double-click. One self-contained file,
#                  no install, runs on most distros.
#
# Snap and Flatpak are intentionally NOT built here: they are *store* channels
# (Snap Store / Flathub) with their own publishing pipelines and review, not
# drop-in release artifacts. Build those via snapcraft/flatpak-builder when
# you're ready to publish to the stores.
#
# Must run on Linux (needs dpkg-deb/dpkg-shlibdeps + appimagetool). Designed to
# run on the same build box right after package_linux.sh.
#
# Usage:
#   ./scripts/package_linux_installers.sh [path-to-out-dir] [version] [arch]
# Defaults mirror package_linux.sh (out=../chromium/src/out/aether_linux,
# version=dev, arch=x64).
set -eu

XPLORER="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$XPLORER/../chromium/src/out/aether_linux}"
VERSION="${2:-dev}"
ARCH="${3:-x64}"

DIST="$XPLORER/dist"
NAME="Xplorer-linux-$ARCH"
STAGED="$DIST/$NAME"          # the tree package_linux.sh builds
DEB_VER="${VERSION#v}"        # deb Version field: strip any leading 'v'
DEB_ARCH="amd64"             # x64 -> amd64 in dpkg terms
APP_ARCH="x86_64"           # AppImage arch token

[ "$(uname)" = "Linux" ] || { echo "package_linux_installers.sh must run on Linux." >&2; exit 1; }

# Stage the tree if package_linux.sh hasn't run yet in this session.
if [ ! -d "$STAGED" ]; then
  echo "Staged tree $STAGED not found — running package_linux.sh first ..."
  "$XPLORER/scripts/package_linux.sh" "$OUT" "$VERSION" "$ARCH"
fi
[ -x "$STAGED/chrome" ] || [ -f "$STAGED/chrome" ] || { echo "No chrome in $STAGED" >&2; exit 1; }

sha() { ( cd "$DIST" && { command -v sha256sum >/dev/null 2>&1 && sha256sum "$1" || shasum -a 256 "$1"; } > "$1.sha256.txt" ); }

# Map a product_logo_NN.png to a hicolor icon dir; install all sizes we have.
install_icons() {  # $1 = dest icons root (…/share/icons)
  local dest="$1" f sz
  for f in "$STAGED"/product_logo_*.png; do
    [ -e "$f" ] || continue
    sz="$(basename "$f" | sed -E 's/product_logo_([0-9]+)\.png/\1/')"
    case "$sz" in 16|24|32|48|64|128|256) ;; *) continue ;; esac
    mkdir -p "$dest/hicolor/${sz}x${sz}/apps"
    cp -a "$f" "$dest/hicolor/${sz}x${sz}/apps/xplorer.png"
  done
}

# ===========================================================================
# 1) .deb
# ===========================================================================
build_deb() {
  command -v dpkg-deb >/dev/null 2>&1 || { echo "WARN: dpkg-deb missing — skipping .deb"; return 0; }
  echo "=== Building .deb (xplorer ${DEB_VER} ${DEB_ARCH}) ==="
  local PKG="$DIST/deb/${NAME}"
  rm -rf "$PKG"; mkdir -p "$PKG/DEBIAN" "$PKG/opt/Xplorer" \
    "$PKG/usr/bin" "$PKG/usr/share/applications" "$PKG/usr/share/icons"

  # Payload: the whole staged tree under /opt/Xplorer.
  cp -a "$STAGED/." "$PKG/opt/Xplorer/"
  # chrome needs a "chrome-sandbox" beside it; ship it and SUID it in postinst.
  if [ ! -e "$PKG/opt/Xplorer/chrome-sandbox" ] && [ -e "$PKG/opt/Xplorer/chrome_sandbox" ]; then
    cp -a "$PKG/opt/Xplorer/chrome_sandbox" "$PKG/opt/Xplorer/chrome-sandbox"
  fi
  ln -sf /opt/Xplorer/xplorer "$PKG/usr/bin/xplorer"
  install_icons "$PKG/usr/share/icons"

  # Menu entry with absolute Exec/Icon (no @@INSTALL_DIR@@ placeholder).
  cat > "$PKG/usr/share/applications/xplorer.desktop" <<DESK
[Desktop Entry]
Version=1.0
Type=Application
Name=Xplorer
GenericName=Web Browser
Comment=Xplorer — a Grok-native Chromium browser
Exec=/opt/Xplorer/xplorer %U
Icon=xplorer
Terminal=false
Categories=Network;WebBrowser;
MimeType=text/html;text/xml;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
StartupNotify=true
StartupWMClass=Xplorer
DESK

  # Runtime Depends, derived from the actual binaries (correct per-platform lib
  # names incl. Ubuntu 24.04's t64 transition). Fall back to a curated list.
  local DEPS=""
  if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    ( cd "$PKG" && mkdir -p debian && printf 'Source: xplorer\n' > debian/control
      dpkg-shlibdeps -O --ignore-missing-info opt/Xplorer/chrome opt/Xplorer/*.so* 2>/dev/null \
        | sed -n 's/^shlibs:Depends=//p' ) > "$DIST/.deb-deps" 2>/dev/null || true
    DEPS="$(cat "$DIST/.deb-deps" 2>/dev/null || true)"; rm -rf "$PKG/debian" "$DIST/.deb-deps"
  fi
  [ -n "$DEPS" ] || DEPS="libnss3, libnspr4, libgbm1, libdrm2, libxkbcommon0, libxcomposite1, libxdamage1, libxrandr2, libxfixes3, libxext6, libx11-6, libxcb1, libcairo2, libpango-1.0-0, libasound2t64 | libasound2, libcups2t64 | libcups2, libatk1.0-0t64 | libatk1.0-0, libatk-bridge2.0-0t64 | libatk-bridge2.0-0, libgtk-3-0t64 | libgtk-3-0, libglib2.0-0t64 | libglib2.0-0, libdbus-1-3"

  local SIZE_KB; SIZE_KB="$(du -sk "$PKG/opt" "$PKG/usr" | awk '{s+=$1} END{print s}')"
  cat > "$PKG/DEBIAN/control" <<CTRL
Package: xplorer
Version: ${DEB_VER}
Architecture: ${DEB_ARCH}
Maintainer: Daniel Farina <elchileno@gmail.com>
Installed-Size: ${SIZE_KB}
Depends: ${DEPS}
Section: web
Priority: optional
Homepage: https://github.com/daniel-farina/xplorer
Description: Xplorer — a Grok-native Chromium browser
 A source-level Chromium fork with an integrated native AI toolbar and a local
 agent gateway. This package installs Xplorer under /opt/Xplorer with a
 /usr/bin/xplorer launcher and a desktop menu entry.
CTRL

  cat > "$PKG/DEBIAN/postinst" <<'POST'
#!/bin/sh
set -e
# SUID the sandbox helper so the setuid sandbox works on hosts where the
# unprivileged userns sandbox is disabled (matches google-chrome.deb).
if [ -e /opt/Xplorer/chrome-sandbox ]; then
  chown root:root /opt/Xplorer/chrome-sandbox || true
  chmod 4755 /opt/Xplorer/chrome-sandbox || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
exit 0
POST
  cat > "$PKG/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
  fi
fi
exit 0
POSTRM
  chmod 0755 "$PKG/DEBIAN/postinst" "$PKG/DEBIAN/postrm"

  local DEB="$DIST/${NAME}.deb"
  if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --build --root-owner-group "$PKG" "$DEB"
  else
    dpkg-deb --build --root-owner-group "$PKG" "$DEB"
  fi
  sha "${NAME}.deb"
  echo "  -> $DEB"
  command -v dpkg-deb >/dev/null && dpkg-deb -I "$DEB" | sed 's/^/    /' || true
}

# ===========================================================================
# 2) AppImage
# ===========================================================================
build_appimage() {
  echo "=== Building AppImage (${APP_ARCH}) ==="
  local TOOL="$DIST/appimagetool-${APP_ARCH}.AppImage"
  if [ ! -x "$TOOL" ]; then
    local urls="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${APP_ARCH}.AppImage https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${APP_ARCH}.AppImage"
    for u in $urls; do
      echo "  downloading appimagetool: $u"
      if wget -q -O "$TOOL" "$u" && [ -s "$TOOL" ]; then chmod +x "$TOOL"; break; fi
    done
  fi
  [ -x "$TOOL" ] || { echo "WARN: appimagetool unavailable — skipping AppImage"; return 0; }

  # appimagetool needs FUSE; on a server use --appimage-extract-and-run.
  export APPIMAGE_EXTRACT_AND_RUN=1

  local AD="$DIST/AppDir"
  rm -rf "$AD"; mkdir -p "$AD/usr/lib/xplorer" "$AD/usr/share/applications" "$AD/usr/share/icons"
  cp -a "$STAGED/." "$AD/usr/lib/xplorer/"
  if [ ! -e "$AD/usr/lib/xplorer/chrome-sandbox" ] && [ -e "$AD/usr/lib/xplorer/chrome_sandbox" ]; then
    cp -a "$AD/usr/lib/xplorer/chrome_sandbox" "$AD/usr/lib/xplorer/chrome-sandbox"
  fi
  install_icons "$AD/usr/share/icons"

  cat > "$AD/AppRun" <<'RUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/lib/xplorer/xplorer" "$@"
RUN
  chmod +x "$AD/AppRun"

  cat > "$AD/xplorer.desktop" <<DESK
[Desktop Entry]
Version=1.0
Type=Application
Name=Xplorer
GenericName=Web Browser
Comment=Xplorer — a Grok-native Chromium browser
Exec=xplorer %U
Icon=xplorer
Terminal=false
Categories=Network;WebBrowser;
MimeType=text/html;text/xml;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
StartupNotify=true
StartupWMClass=Xplorer
DESK
  cp "$AD/xplorer.desktop" "$AD/usr/share/applications/xplorer.desktop"

  # Top-level icon (AppImage requires .DirIcon / a root icon named like the desktop Icon=).
  local topicon=""
  for sz in 256 128 64 48; do
    if [ -e "$AD/usr/share/icons/hicolor/${sz}x${sz}/apps/xplorer.png" ]; then
      topicon="$AD/usr/share/icons/hicolor/${sz}x${sz}/apps/xplorer.png"; break
    fi
  done
  if [ -n "$topicon" ]; then cp "$topicon" "$AD/xplorer.png"; cp "$topicon" "$AD/.DirIcon"; fi

  local OUTIMG="$DIST/${NAME%-x64}-${APP_ARCH}.AppImage"   # -> Xplorer-linux-x86_64.AppImage
  if ARCH="$APP_ARCH" "$TOOL" "$AD" "$OUTIMG"; then
    sha "$(basename "$OUTIMG")"
    echo "  -> $OUTIMG"
  else
    echo "WARN: appimagetool failed — AppImage not produced"
  fi
}

build_deb
build_appimage

echo "=== installer artifacts in $DIST ==="
ls -lh "$DIST"/*.deb "$DIST"/*.AppImage 2>/dev/null || true
