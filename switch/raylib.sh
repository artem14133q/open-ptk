#!/bin/sh
# Gets raylib-nx (raylib's Nintendo Switch port, by Luiz Pestana) at the same
# raylib version the desktop build uses, and builds it as a static library in
# deps/raylib-nx/src. Its own gamepad-to-keyboard emulation is switched off:
# the launcher maps the controller itself (launcher/src/platform.c).
set -e
cd "$(dirname "$0")"
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
TAG=6.0
if [ ! -d deps/raylib-nx/src ]; then
    mkdir -p deps
    git clone --depth 1 --branch "$TAG" https://github.com/luizpestana/raylib-nx.git deps/raylib-nx
fi
make -C deps/raylib-nx/src PLATFORM=PLATFORM_NX CUSTOM_CFLAGS="-DNX_DISABLE_GAMEPAD_EMULATION" -j8
echo "raylib-nx: deps/raylib-nx/src/libraylib.a"
