#!/bin/sh
# Package a built Linux Chromium ("chrome" + runtime files) into a portable
# .tar.gz with a SHA-256, analogous to scripts/package.sh (macOS .dmg/.zip) and
# scripts/package.ps1 (Windows portable .zip).
#
# Linux has no .app bundle, so the "portable distributable" is a self-contained
# directory tree (the chrome binary + its shared libs, resource paks, snapshot
# blobs, ICU data, locales, the crashpad handler, and the bundled companion UI)
# that runs in place from anywhere. chrome is launched via a small "xplorer"
# wrapper so the shipped/visible command reads "xplorer"; the wrapper resolves
# its own dir and execs the real chrome binary beside it.
#
# Usage:
#   ./scripts/package_linux.sh [path-to-out-dir] [version] [arch]
#
# Defaults:
#   out-dir : ../chromium/src/out/aether_linux  (relative to the repo)
#   version : dev
#   arch    : x64
#
# Examples:
#   ./scripts/package_linux.sh
#   ./scripts/package_linux.sh "$(pwd)/chromium/src/out/aether_linux" v0.7.0 x64
set -eu

XPLORER="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$XPLORER/../chromium/src/out/aether_linux}"
VERSION="${2:-dev}"
ARCH="${3:-x64}"

CHROME="$OUT/chrome"
[ -x "$CHROME" ] || [ -f "$CHROME" ] || {
  echo "No 'chrome' binary at $CHROME — build the Linux target first." >&2
  exit 1
}

NAME="Xplorer-linux-$ARCH"
DIST="$XPLORER/dist"
ROOT="$DIST/$NAME"          # staged tree -> tars to $NAME.tar.gz
UI_SRC="$XPLORER/companion/ui"

echo "Packaging $NAME (version $VERSION) from $OUT ..."

# Clean stage. The out dir also holds many GB of obj/, gen/, *.a, *.o build
# intermediates we must NOT ship — so we whitelist-copy named runtime files
# rather than copying the whole dir.
rm -rf "$ROOT"
mkdir -p "$ROOT"

# --- helpers ----------------------------------------------------------------
# Copy a top-level runtime file iff it exists (robust: missing optional files
# are skipped, not fatal). Preserves the basename under $ROOT.
copy_file() {
  src="$OUT/$1"
  if [ -f "$src" ]; then
    cp -a "$src" "$ROOT/$1"
  fi
}

# Copy a top-level dir iff it exists.
copy_dir() {
  src="$OUT/$1"
  if [ -d "$src" ]; then
    cp -a "$src" "$ROOT/$1"
  fi
}

# Copy by glob (e.g. *.so, *.so.*); silently no-ops when nothing matches.
copy_glob() {
  for src in "$OUT"/$1; do
    [ -e "$src" ] || continue   # unmatched glob stays literal -> skip
    cp -a "$src" "$ROOT/"
  done
}

# --- the chrome binary ------------------------------------------------------
# Ship the real binary under its build name so all of Chromium's own DIR_EXE /
# version-dir assumptions hold; the user-facing entry point is the "xplorer"
# wrapper added below.
cp -a "$CHROME" "$ROOT/chrome"
chmod +x "$ROOT/chrome"

# chrome_sandbox -> the SUID sandbox helper. Optional (modern kernels use the
# namespace sandbox), but ship it if present so --sandbox works on older hosts.
# It must be installed root:root mode 4755 to actually function; a portable
# tarball can't carry SUID, so we ship it un-SUID and note it in the README.
copy_file chrome_sandbox
copy_file chrome-sandbox      # some builds name it with a dash

# Out-of-process crash reporter (no extension on Linux).
copy_file chrome_crashpad_handler

# --- shared libraries (ANGLE / GL / Vulkan / SwiftShader) -------------------
# ANGLE GLES front-ends and the Vulkan loader + SwiftShader software ICD. The
# *.so.<n> forms (libvulkan.so.1) matter — copy both bare and versioned names.
copy_file libEGL.so
copy_file libGLESv2.so
copy_file libvulkan.so.1
copy_file libvk_swiftshader.so
copy_file vk_swiftshader_icd.json     # the ICD manifest SwiftShader loads by
copy_file libVkICD_mock_icd.so        # (only present in some configs)
copy_file libvulkan.so                # bare name, if the build emits it

# Optional dlopen()ed system-integration libs some configs bundle.
copy_file libGLESv2.so.2
copy_file libEGL.so.1
# Catch any other top-level .so / .so.<ver> the build produced (robust against
# Chromium adding/renaming shared libs between milestones). obj/ and gen/ live
# in subdirs, so this top-level-only glob won't pull in intermediates.
copy_glob '*.so'
copy_glob '*.so.*'

# --- resource paks / data blobs / ICU ---------------------------------------
copy_file chrome_100_percent.pak
copy_file chrome_200_percent.pak
copy_file resources.pak
copy_file icudtl.dat                  # ICU data (i18n); chrome aborts without it
copy_file v8_context_snapshot.bin     # V8 snapshot — name depends on GN args:
copy_file snapshot_blob.bin           #   one of these two is present, not both
# Headless / extra paks some configs emit; harmless if absent.
copy_file headless_lib_data.pak
copy_file headless_lib_strings.pak

# Any other top-level *.pak / *.bin / *.dat the build produced.
copy_glob '*.pak'
copy_glob '*.bin'
copy_glob '*.dat'

# --- locales ----------------------------------------------------------------
# locales/<lang>.pak — localized strings. en-US.pak is the bare minimum; ship
# the whole dir so every UI language works.
copy_dir locales

# --- product logos / icons --------------------------------------------------
# product_logo_NN.png are the app icons referenced by the .desktop file and
# chrome://-internal logo resources. Names vary (product_logo_*.png); copy all.
copy_glob 'product_logo_*.png'

# --- optional resources subtree (extensions/component data, MEIPreload, etc.) -
copy_dir resources
copy_dir MEIPreload
copy_dir WidevineCdm                  # only present in builds with Widevine

# --- companion UI -----------------------------------------------------------
# The gateway resolves its UI via UiDir(). On Linux the in-binary fallback only
# checks the macOS-style ../Resources/companion/ui layout, so for this flat tree
# we set XPLORER_COMPANION_UI in the wrapper (env var wins over every fallback)
# and ALSO stage the UI both flat (companion/ui) and under a Resources/ dir so
# any of the resolution paths find it. Self-contained on any machine.
if [ -d "$UI_SRC" ]; then
  mkdir -p "$ROOT/companion"
  cp -a "$UI_SRC" "$ROOT/companion/ui"
  # Mirror the macOS layout DIR_EXE/../Resources/companion/ui resolves to: with
  # the wrapper exec'ing $ROOT/chrome, DIR_EXE is $ROOT, so ../Resources is the
  # parent of $ROOT — not inside the tarball. The env var below is what makes it
  # work regardless; this copy is a belt-and-suspenders for direct ./chrome runs
  # from one level down. Cheap (tens of KB), so keep it.
  mkdir -p "$ROOT/Resources/companion"
  cp -a "$UI_SRC" "$ROOT/Resources/companion/ui"
else
  echo "WARN: companion UI not found at $UI_SRC — UI routes will 401." >&2
fi

# --- xplorer launcher wrapper ----------------------------------------------
# Small POSIX-sh wrapper: resolves its own real dir (following symlinks), points
# the gateway at the bundled companion UI, and execs the real chrome binary,
# forwarding all args. This is the command users run; the visible process keeps
# the chrome name but the entry point reads "xplorer".
cat > "$ROOT/xplorer" <<'WRAP'
#!/bin/sh
# Xplorer portable launcher. Resolves the bundle dir even when invoked via a
# symlink (e.g. /usr/local/bin/xplorer -> .../Xplorer-linux-x64/xplorer), then
# execs the bundled chrome with the companion UI wired up.
set -eu

# Resolve this script's real directory, following one or more symlinks.
SELF="$0"
while [ -h "$SELF" ]; do
  link="$(readlink "$SELF")"
  case "$link" in
    /*) SELF="$link" ;;
    *)  SELF="$(dirname "$SELF")/$link" ;;
  esac
done
HERE="$(cd "$(dirname "$SELF")" && pwd)"

# Point the gateway at the bundled companion UI (env var wins over all in-binary
# fallbacks), unless the user already set one.
if [ -z "${XPLORER_COMPANION_UI:-}" ] && [ -d "$HERE/companion/ui" ]; then
  XPLORER_COMPANION_UI="$HERE/companion/ui"
  export XPLORER_COMPANION_UI
fi

# Help the dynamic loader find bundled .so files that aren't in a version dir.
LD_LIBRARY_PATH="$HERE${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH

exec "$HERE/chrome" "$@"
WRAP
chmod +x "$ROOT/xplorer"

# --- .desktop entry ---------------------------------------------------------
# A minimal freedesktop .desktop file. Exec/Icon use a literal path token the
# installer (or the user) rewrites to the install location; %U forwards URLs.
DESKTOP_ICON="xplorer"
for sz in 256 128 64 48 32 24 16; do
  if [ -f "$ROOT/product_logo_${sz}.png" ]; then
    DESKTOP_ICON="@@INSTALL_DIR@@/product_logo_${sz}.png"
    break
  fi
done

cat > "$ROOT/xplorer.desktop" <<DESKTOP
[Desktop Entry]
Version=1.0
Type=Application
Name=Xplorer
GenericName=Web Browser
Comment=Xplorer — a Grok-native Chromium browser
Exec=@@INSTALL_DIR@@/xplorer %U
Icon=$DESKTOP_ICON
Terminal=false
Categories=Network;WebBrowser;
MimeType=text/html;text/xml;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
StartupNotify=true
StartupWMClass=Xplorer
DESKTOP

# --- install hint -----------------------------------------------------------
# Quick note so users know how to wire the .desktop file and the SUID sandbox.
cat > "$ROOT/README.txt" <<README
Xplorer (portable Linux build, $ARCH, version $VERSION)

Run:
    ./xplorer

Install the menu entry (optional): pick an install dir, then rewrite the
@@INSTALL_DIR@@ placeholders in xplorer.desktop to that path and copy it to
~/.local/share/applications/ :

    sed "s|@@INSTALL_DIR@@|\$PWD|g" xplorer.desktop \\
        > ~/.local/share/applications/xplorer.desktop

Sandbox: this tarball ships chrome_sandbox without the SUID bit (tarballs can't
carry it). Modern kernels use the unprivileged namespace sandbox and need no
setup. On hosts where that is disabled, either run with --no-sandbox or:

    sudo chown root:root chrome_sandbox && sudo chmod 4755 chrome_sandbox
README

# --- fail loudly on missing load-bearing files ------------------------------
# Whitelist staging can silently drop a newly required file across milestones;
# bail rather than ship a tarball that won't launch.
missing=
for f in chrome icudtl.dat resources.pak chrome_100_percent.pak; do
  [ -e "$ROOT/$f" ] || missing="$missing $f"
done
# Exactly one V8 snapshot blob must be present.
if [ ! -e "$ROOT/v8_context_snapshot.bin" ] && [ ! -e "$ROOT/snapshot_blob.bin" ]; then
  missing="$missing v8_context_snapshot.bin|snapshot_blob.bin"
fi
# locales must have at least en-US.pak.
if [ ! -e "$ROOT/locales/en-US.pak" ]; then
  missing="$missing locales/en-US.pak"
fi
# SwiftShader ICD .json must accompany its .so when the .so is shipped.
if [ -e "$ROOT/libvk_swiftshader.so" ] && [ ! -e "$ROOT/vk_swiftshader_icd.json" ]; then
  missing="$missing vk_swiftshader_icd.json(for libvk_swiftshader.so)"
fi
if [ -n "$missing" ]; then
  echo "Staged tree missing required runtime files:$missing" >&2
  exit 1
fi

# --- tar + checksum ---------------------------------------------------------
mkdir -p "$DIST"
TARBALL="$DIST/$NAME.tar.gz"
echo "Creating $TARBALL ..."
# -C $DIST so the archive holds a single top-level "$NAME/" dir (no tarbomb).
tar -C "$DIST" -czf "$TARBALL" "$NAME"

echo "Checksum..."
( cd "$DIST" && {
    if command -v sha256sum >/dev/null 2>&1; then
      sha256sum "$NAME.tar.gz"
    else
      shasum -a 256 "$NAME.tar.gz"   # macOS / BSD fallback
    fi
  } > "$NAME.sha256.txt" )

echo "Artifacts in $DIST:"
ls -lh "$DIST" | grep "$NAME" || true
echo "version: $VERSION"
