#!/usr/bin/env bash
# Build vkBasalt for a glibc userspace on Android.
#
# Targets termux-glibc / glibc-runner / a proot Ubuntu, i.e. a real glibc prefix
# instead of the bionic one Termux ships by default.
#
# Usage:
#   ./scripts/build-termux-glibc.sh                 # auto detect
#   VKBASALT_PREFIX=/usr ./scripts/build-termux-glibc.sh
#   VKBASALT_X11=0 ./scripts/build-termux-glibc.sh  # force no X11 backend
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PREFIX="${VKBASALT_PREFIX:-${PREFIX:-/data/data/com.termux/files/usr}}"
BUILD_DIR="${BUILD_DIR:-build-termux}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

command -v meson >/dev/null 2>&1 || die "meson not found (pkg install meson / pacman -S meson)"
command -v ninja >/dev/null 2>&1 || die "ninja not found"

# glslangValidator is needed at build time to turn the .glsl files into headers
GLSLANG_OPT=()
if command -v glslangValidator >/dev/null 2>&1; then
    say "glslangValidator: $(command -v glslangValidator)"
else
    for candidate in /usr/bin/glslangValidator "$PREFIX/bin/glslangValidator"; do
        if [ -x "$candidate" ]; then
            GLSLANG_OPT=(-Dglslang="$candidate")
            say "glslangValidator: $candidate"
            break
        fi
    done
    [ ${#GLSLANG_OPT[@]} -gt 0 ] || die "glslangValidator not found (pkg install glslang / pacman -S glslang)"
fi

# The X11 backend is only used for the toggle hotkey. Inside a proot there is
# usually no libX11 for the glibc prefix, so fall back to no hotkey instead of
# failing the build.
if [ "${VKBASALT_X11:-auto}" = "1" ]; then
    X11_OPT=(-Dx11=true)
elif [ "${VKBASALT_X11:-auto}" = "0" ]; then
    X11_OPT=(-Dx11=false)
elif pkg-config --exists x11 2>/dev/null; then
    X11_OPT=(-Dx11=true)
else
    X11_OPT=(-Dx11=false)
fi
say "x11 backend: ${X11_OPT[*]}"

say "prefix: $PREFIX"
say "build dir: $BUILD_DIR"

meson setup "$BUILD_DIR" \
    --prefix="$PREFIX" \
    --libdir=lib \
    --buildtype=release \
    -Dappend_libdir_vkbasalt=true \
    "${X11_OPT[@]}" "${GLSLANG_OPT[@]}" \
    --wipe

ninja -C "$BUILD_DIR" -j "$JOBS"
ninja -C "$BUILD_DIR" install

LAYER_DIR="$PREFIX/share/vulkan/implicit_layer.d"
MANIFEST="$LAYER_DIR/vkBasalt.json"
LIB="$PREFIX/lib/vkbasalt/libvkbasalt.so"
CONF="$PREFIX/etc/vkBasalt.conf"

[ -f "$MANIFEST" ] || die "manifest not installed to $MANIFEST"
[ -f "$LIB" ] || die "library not installed to $LIB"
mkdir -p "$PREFIX/etc"
[ -f "$CONF" ] || cp config/vkBasalt.conf "$CONF"

cat <<EOF

Installed:
  $LIB
  $MANIFEST
  $CONF

Enable it for a process:

  export VK_LAYER_PATH=$LAYER_DIR
  export ENABLE_VKBASALT=1
  export VKBASALT_CONFIG_FILE=$CONF

If the loader refuses to pick up the layer (newer Android loaders ignore
VK_LAYER_PATH unless the process is debuggable), try:

  export VK_LOADER_LAYERS_ENABLE=VK_LAYER_VKBASALT_post_processing

Verify with:

  VK_LOADER_DEBUG=layer vkcube

Upscaling example (render at 720p, upscale to the 1080p surface):

  upscaleFactor = 0.6667
  effects = cas
EOF