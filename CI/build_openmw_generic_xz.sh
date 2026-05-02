#!/usr/bin/env bash
# build_openmw_generic_xz.sh
#
# Build + package a "generic" OpenMW bundle as .tar.xz.
# Run from the OpenMW repo root

set -euo pipefail

ROOT="$(pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/_build_release}"
STAGE_DIR="${STAGE_DIR:-$ROOT/_pkg_stage}"
BUNDLE_DIR="${BUNDLE_DIR:-$ROOT/OpenMW-XBOX-UI}"
OUT_TAR="${OUT_TAR:-$ROOT/OpenMW-XBOX-UI.tar.xz}"
XZ_LEVEL="${XZ_LEVEL:-9}"
XZ_EXTREME="${XZ_EXTREME:-0}"
ARCHIVE_TAR="${ARCHIVE_TAR:-0}"

die(){ echo "ERROR: $*" >&2; exit 1; }
have(){ command -v "$1" >/dev/null 2>&1; }

[ -f "$ROOT/CMakeLists.txt" ] || die "Run from repo root (CMakeLists.txt missing)."
have cmake  || die "cmake not found"
have ninja  || die "ninja not found"
have tar    || die "tar not found"
have strip  || die "strip not found (install binutils)"
have ldd    || die "ldd not found"
have awk    || die "awk not found"
have sed    || die "sed not found"
have find   || die "find not found"
have xargs  || die "xargs not found"
have readlink || die "readlink not found"
have ls || die "ls not found"

echo "==> Configure"
mkdir -p "$BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "==> Build"
ninja -C "$BUILD_DIR"

echo "==> Stage install"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR"

PREFIX=""
if [ -d "$STAGE_DIR/usr/local" ]; then
  PREFIX="$STAGE_DIR/usr/local"
elif [ -d "$STAGE_DIR/usr" ]; then
  PREFIX="$STAGE_DIR/usr"
else
  find "$STAGE_DIR" -maxdepth 4 -print
  die "Couldn't find staged /usr or /usr/local"
fi
echo "==> Prefix: ${PREFIX#$STAGE_DIR}"

echo "==> Bundle layout"
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR"/{lib,resources,plugins,qt}

# Binaries
BIN_SRC=""
if [ -d "$PREFIX/games/bin" ]; then
  BIN_SRC="$PREFIX/games/bin"
elif [ -d "$PREFIX/bin" ]; then
  BIN_SRC="$PREFIX/bin"
else
  find "$PREFIX" -maxdepth 3 -type d -print
  die "Couldn't find staged binaries"
fi
cp -a "$BIN_SRC/"* "$BUNDLE_DIR/"

pushd "$BUNDLE_DIR" >/dev/null
[ -f ./openmw ] && mv ./openmw ./OpenMW.x86_64
[ -f ./openmw-launcher ] && mv ./openmw-launcher ./OpenMW-Launcher.x86_64
chmod +x ./OpenMW.x86_64 ./OpenMW-Launcher.x86_64 2>/dev/null || true
popd >/dev/null

# Resources
RES_SRC=""
if [ -d "$PREFIX/share/games/openmw/resources" ]; then
  RES_SRC="$PREFIX/share/games/openmw/resources"
elif [ -d "$PREFIX/share/openmw/resources" ]; then
  RES_SRC="$PREFIX/share/openmw/resources"
fi
[ -n "$RES_SRC" ] || die "Couldn't find staged resources"
cp -a "$RES_SRC/"* "$BUNDLE_DIR/resources/"

# Files that need to be in bundle root (find them anywhere in stage)
copy_named_from_stage() {
  local name="$1"
  local src=""
  src="$(find "$STAGE_DIR" -type f -name "$name" -print -quit 2>/dev/null || true)"
  if [ -n "$src" ]; then
    echo "==> Copy $name"
    cp -a "$src" "$BUNDLE_DIR/"
  else
    echo "WARN: $name not found in stage"
  fi
}
copy_named_from_stage "defaults.bin"
copy_named_from_stage "defaults-cs.bin"
copy_named_from_stage "gamecontrollerdb.txt"
[ -f "$BUNDLE_DIR/defaults.bin" ] || die "defaults.bin missing in bundle root"

# OpenMW plugin dir (if present)
[ -d "$PREFIX/lib/x86_64-linux-gnu/openmw" ] && cp -a "$PREFIX/lib/x86_64-linux-gnu/openmw/"* "$BUNDLE_DIR/plugins/" || true
[ -d "$PREFIX/lib/openmw" ] && cp -a "$PREFIX/lib/openmw/"* "$BUNDLE_DIR/plugins/" || true

# Qt6 platform plugins
QT_PLUGINS_SRC=""
for d in /usr/lib/x86_64-linux-gnu/qt6/plugins /usr/lib/qt6/plugins; do
  if [ -d "$d/platforms" ]; then QT_PLUGINS_SRC="$d"; break; fi
done
if [ -n "$QT_PLUGINS_SRC" ]; then
  mkdir -p "$BUNDLE_DIR/qt/plugins"
  cp -a "$QT_PLUGINS_SRC/platforms" "$BUNDLE_DIR/qt/plugins/"
  [ -d "$QT_PLUGINS_SRC/wayland-shell-integration" ] && cp -a "$QT_PLUGINS_SRC/wayland-shell-integration" "$BUNDLE_DIR/qt/plugins/" || true
  [ -d "$QT_PLUGINS_SRC/wayland-graphics-integration-client" ] && cp -a "$QT_PLUGINS_SRC/wayland-graphics-integration-client" "$BUNDLE_DIR/qt/plugins/" || true
  [ -d "$QT_PLUGINS_SRC/wayland-decoration-client" ] && cp -a "$QT_PLUGINS_SRC/wayland-decoration-client" "$BUNDLE_DIR/qt/plugins/" || true
else
  echo "WARN: Qt6 plugins dir not found"
fi

# OSG plugins
OSG_PLUGINS_SRC=""
for base in /usr/lib/x86_64-linux-gnu /usr/lib; do
  p="$(ls -d "$base"/osgPlugins-* 2>/dev/null | head -n 1 || true)"
  if [ -n "$p" ] && [ -d "$p" ]; then OSG_PLUGINS_SRC="$p"; break; fi
done
if [ -n "$OSG_PLUGINS_SRC" ]; then
  cp -a "$OSG_PLUGINS_SRC" "$BUNDLE_DIR/"
else
  echo "WARN: osgPlugins-* not found"
fi

# openmw.cfg: take staged one, patch the path lines
STAGED_CFG="$(find "$STAGE_DIR" -type f -name 'openmw.cfg' -print -quit 2>/dev/null || true)"
[ -n "$STAGED_CFG" ] || die "Couldn't find staged openmw.cfg"
cp -a "$STAGED_CFG" "$BUNDLE_DIR/openmw.cfg"

sed -i \
  -e 's|^config=.*|config=?userconfig?|' \
  -e 's|^user-data=.*|user-data=?userdata?|' \
  -e 's|^data-local=.*|data-local=?userdata?data|' \
  -e 's|^resources=.*|resources=./resources|' \
  -e 's|/usr/local/share/games/openmw/resources|./resources|g' \
  -e 's|/usr/share/games/openmw/resources|./resources|g' \
  "$BUNDLE_DIR/openmw.cfg"

grep -q '^data=\./resources/vfs-mw$' "$BUNDLE_DIR/openmw.cfg" || echo 'data=./resources/vfs-mw' >> "$BUNDLE_DIR/openmw.cfg"

# Bundle libs
pushd "$BUNDLE_DIR" >/dev/null

skip_lib() {
  case "$1" in
    */ld-linux*|*/libc.so*|*/libm.so*|*/libpthread.so*|*/libdl.so*|*/librt.so*) return 0 ;;
  esac
  return 1
}

copy_dep() {
  local dep="$1"
  [ -n "$dep" ] || return 0
  [ -e "$dep" ] || return 0
  cp -aL "$dep" ./lib/ 2>/dev/null || true
  local real
  real="$(readlink -f "$dep" 2>/dev/null || true)"
  [ -n "$real" ] && [ -f "$real" ] && cp -aL "$real" ./lib/ 2>/dev/null || true
}

deps_tmp="$(mktemp)"
ldd ./OpenMW.x86_64 | awk '/=> \//{print $3} /^\//{print $1}' >> "$deps_tmp" || true
ldd ./OpenMW-Launcher.x86_64 | awk '/=> \//{print $3} /^\//{print $1}' >> "$deps_tmp" || true

if [ -d ./qt/plugins/platforms ]; then
  find ./qt/plugins/platforms -type f -name '*.so' -print0 \
    | xargs -0 -I{} sh -c "ldd '{}' | awk '/=> \\/|^\\//{print \$3==\"\"?\$1:\$3}'" \
    >> "$deps_tmp" || true
fi

if ls -d ./osgPlugins-* >/dev/null 2>&1; then
  find ./osgPlugins-* -type f -name '*.so' -print0 \
    | xargs -0 -I{} sh -c "ldd '{}' | awk '/=> \\/|^\\//{print \$3==\"\"?\$1:\$3}'" \
    >> "$deps_tmp" || true
fi

sort -u "$deps_tmp" | while read -r dep; do
  [ -n "$dep" ] || continue
  skip_lib "$dep" && continue
  copy_dep "$dep"
done
rm -f "$deps_tmp"

# FFmpeg
for f in /usr/lib/x86_64-linux-gnu/libavcodec.so.60 \
         /usr/lib/x86_64-linux-gnu/libavformat.so.60 \
         /usr/lib/x86_64-linux-gnu/libavutil.so.58 \
         /usr/lib/x86_64-linux-gnu/libswscale.so.7 \
         /usr/lib/x86_64-linux-gnu/libswresample.so.4; do
  [ -e "$f" ] && copy_dep "$f"
done

# libcurl + OpenSSL (avoid mixing with host)
for f in /usr/lib/x86_64-linux-gnu/libcurl.so.4 \
         /usr/lib/x86_64-linux-gnu/libnghttp2.so.* \
         /usr/lib/x86_64-linux-gnu/libidn2.so.* \
         /usr/lib/x86_64-linux-gnu/libpsl.so.* \
         /usr/lib/x86_64-linux-gnu/libz.so.* \
         /usr/lib/x86_64-linux-gnu/libbrotlidec.so.* \
         /usr/lib/x86_64-linux-gnu/libbrotlicommon.so.* \
         /usr/lib/x86_64-linux-gnu/libssl.so.3 \
         /usr/lib/x86_64-linux-gnu/libcrypto.so.3; do
  [ -e "$f" ] && copy_dep "$f"
done

# libunshield (needed for certain installers)
for f in /usr/lib/x86_64-linux-gnu/libunshield.so.0; do
  [ -e "$f" ] && copy_dep "$f"
done

# Replace any symlinks with real files (for FAT/exFAT compatibility).
while IFS= read -r link; do
  real="$(readlink -f "$link" 2>/dev/null || true)"
  [ -n "$real" ] && [ -f "$real" ] || continue
  rm -f "$link" 2>/dev/null || true
  cp -aL "$real" "$link" 2>/dev/null || true
done < <(find ./lib -type l)

# Wrappers
cat > ./openmw <<'EOW'
#!/bin/sh
set -eu

readlink_sh() {
  path=$1
  if [ -L "$path" ]; then
    ls -l "$path" | sed 's/^.*-> //'
  else
    return 1
  fi
}

SCRIPT="$0"
COUNT=0
while [ -L "$SCRIPT" ]; do
  SCRIPT=$(readlink_sh "$SCRIPT")
  COUNT=$(expr "$COUNT" + 1)
  [ "$COUNT" -le 100 ] || { echo "Too many symbolic links"; exit 1; }
done

GAMEDIR=$(dirname "$SCRIPT")
cd "$GAMEDIR" || { echo "Failed to enter $GAMEDIR"; exit 1; }

export LD_LIBRARY_PATH="$GAMEDIR/lib:${LD_LIBRARY_PATH:-}"
export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
export OSG_LIBRARY_PATH="$GAMEDIR${OSG_LIBRARY_PATH:+:$OSG_LIBRARY_PATH}"

exec "$GAMEDIR/OpenMW.x86_64" --resources "$GAMEDIR/resources" "$@"
EOW
chmod +x ./openmw

cat > ./OpenMW-Launcher <<'EOW'
#!/bin/sh
set -eu

readlink_sh() {
  path=$1
  if [ -L "$path" ]; then
    ls -l "$path" | sed 's/^.*-> //'
  else
    return 1
  fi
}

SCRIPT="$0"
COUNT=0
while [ -L "$SCRIPT" ]; do
  SCRIPT=$(readlink_sh "$SCRIPT")
  COUNT=$(expr "$COUNT" + 1)
  [ "$COUNT" -le 100 ] || { echo "Too many symbolic links"; exit 1; }
done

GAMEDIR=$(dirname "$SCRIPT")
cd "$GAMEDIR" || { echo "Failed to enter $GAMEDIR"; exit 1; }

export LD_LIBRARY_PATH="$GAMEDIR/lib:${LD_LIBRARY_PATH:-}"
export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"

export QT_PLUGIN_PATH="$GAMEDIR/qt/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$GAMEDIR/qt/plugins/platforms"
export OSG_LIBRARY_PATH="$GAMEDIR"

exec "$GAMEDIR/OpenMW-Launcher.x86_64" --resources "$GAMEDIR/resources" "$@"
EOW
chmod +x ./OpenMW-Launcher

echo "==> Strip"
strip --strip-unneeded ./OpenMW.x86_64 ./OpenMW-Launcher.x86_64 2>/dev/null || true
find ./lib -type f -name '*.so*' -exec strip --strip-unneeded {} + 2>/dev/null || true
find ./qt -type f -name '*.so' -exec strip --strip-unneeded {} + 2>/dev/null || true
find . -maxdepth 1 -type d -name 'osgPlugins-*' -print0 2>/dev/null \
  | xargs -0 -I{} find "{}" -type f -name '*.so' -exec strip --strip-unneeded {} + 2>/dev/null || true

popd >/dev/null

if [ "$ARCHIVE_TAR" = "1" ]; then
  echo "==> Tarball: $OUT_TAR"
  rm -f "$OUT_TAR"

  XZ_OPT="-$XZ_LEVEL"
  [ "$XZ_EXTREME" = "1" ] && XZ_OPT="$XZ_OPT -e"
  export XZ_OPT

  tar -cJf "$OUT_TAR" -C "$ROOT" "$(basename "$BUNDLE_DIR")"
  echo "DONE: $OUT_TAR"
else
  echo "DONE: $BUNDLE_DIR"
fi
