#!/bin/sh
# Builds FFmpeg for the Switch as static libraries in deps/ffmpeg, with only
# what the game's films need: the AVI demuxer, Indeo 3 (INTRO.AVI) and
# Microsoft Video 1 (the rest), MS ADPCM and 8/16-bit PCM sound, and the
# scaler and resampler. devkitPro's switch-ffmpeg package would do as well,
# but drags in libass, freetype, dav1d and the rest for nothing.
set -e
cd "$(dirname "$0")"
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export PATH="$DEVKITPRO/devkitA64/bin:$PATH"
TAG=n7.1
SRC=deps/ffmpeg-src
OUT="$(pwd)/deps/ffmpeg"
if [ ! -d "$SRC" ]; then
    mkdir -p deps
    git clone --depth 1 --branch "$TAG" https://github.com/FFmpeg/FFmpeg.git "$SRC"
fi
cd "$SRC"
./configure --prefix="$OUT" \
    --enable-cross-compile --cross-prefix=aarch64-none-elf- --arch=aarch64 --cpu=cortex-a57 \
    --target-os=none --pkg-config=false \
    --extra-cflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ftls-model=local-exec -I$DEVKITPRO/libnx/include" \
    --extra-ldflags="-specs=$DEVKITPRO/libnx/switch.specs -L$DEVKITPRO/libnx/lib" \
    --enable-static --disable-shared --disable-programs --disable-doc --disable-debug \
    --disable-autodetect --disable-network --disable-pthreads --disable-runtime-cpudetect \
    --disable-everything --disable-avdevice --disable-avfilter \
    --enable-protocol=file --enable-demuxer=avi \
    --enable-decoder=indeo3,msvideo1,adpcm_ms,pcm_s16le,pcm_u8 \
    --enable-swscale --enable-swresample
make -j8
make install
echo "ffmpeg: $OUT"
