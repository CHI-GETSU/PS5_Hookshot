#!/usr/bin/env bash
# PS5: FFmpeg's libavcodec, libswscale and libavutil with dav1d, static, for the console: the game's movies are AV1
# (dav1d) and the codec service also decodes VP8 and VP9 (host/nn_video.c). The versions are the Linux AppImage's
# (scripts/linux/deps.sh); the configure line is PS5_RetroArch's tools/build-ffmpeg.sh cut down to these decoders.
#
#   ps5/tools/build-ffmpeg.sh            # into .local/ps5-deps/ffmpeg (skipped when built from these versions)
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
deps="$root/.local/ps5-deps"
FFMPEG_VER=9.0.2
DAV1D_VER=1.5.4
prefix="$deps/ffmpeg"
stamp="$FFMPEG_VER $DAV1D_VER $(sha256sum "$0" | cut -c1-16)"
[[ -f $prefix/.stamp && $(<"$prefix/.stamp") == "$stamp" ]] && { echo "ffmpeg: built already ($prefix)"; exit 0; }
sdk=${PS5_PAYLOAD_SDK:-$root/../PS5_VulkanTemplate/.deps/native/ps5-payload-sdk}
[[ -x $sdk/bin/prospero-clang ]] || { echo "no PS5 payload SDK at $sdk (PS5_PAYLOAD_SDK)" >&2; exit 2; }
export PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=${PS5_CLANG:-$(command -v clang)}
nasm=$(command -v nasm) || { echo "nasm is needed (dav1d's and FFmpeg's assembly)" >&2; exit 2; }
build="$deps/build-ffmpeg"
rm -rf -- "$build"
mkdir -p "$build" "$deps/downloads"

fetch() {   # url file
    [[ -f $deps/downloads/$2 ]] || curl -fL --retry 5 --retry-all-errors --proto '=https' -o "$deps/downloads/$2.part" "$1"
    [[ -f $deps/downloads/$2.part ]] && mv "$deps/downloads/$2.part" "$deps/downloads/$2"
    return 0
}

# dav1d, with meson and the SDK's compilers (PS5_Vulkan's cross file shape)
fetch "https://code.videolan.org/videolan/dav1d/-/archive/$DAV1D_VER/dav1d-$DAV1D_VER.tar.gz" "dav1d-$DAV1D_VER.tar.gz"
mkdir -p "$build/dav1d"
tar -xzf "$deps/downloads/dav1d-$DAV1D_VER.tar.gz" -C "$build/dav1d" --strip-components=1
cat > "$build/cross.ini" <<EOF
[binaries]
c = '$sdk/bin/prospero-clang'
cpp = '$sdk/bin/prospero-clang++'
ar = '$sdk/bin/prospero-ar'
strip = '$sdk/bin/prospero-strip'
nasm = '$nasm'

[host_machine]
system = 'freebsd'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[built-in options]
default_library = 'static'
c_args = ['-march=znver2', '-fno-omit-frame-pointer']

[properties]
needs_exe_wrapper = true
EOF
meson setup "$build/dav1d-build" "$build/dav1d" --cross-file "$build/cross.ini" --prefix="$prefix" --libdir=lib \
    --buildtype=release -Denable_tools=false -Denable_tests=false -Denable_examples=false > "$build/dav1d.log" 2>&1 ||
    { tail -30 "$build/dav1d.log" >&2; exit 1; }
ninja -C "$build/dav1d-build" install >> "$build/dav1d.log" 2>&1 || { tail -30 "$build/dav1d.log" >&2; exit 1; }

# FFmpeg: the decoders host/nn_video.c asks for, and swscale's NV12 to RGBA
fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VER.tar.xz" "ffmpeg-$FFMPEG_VER.tar.xz"
mkdir -p "$build/ffmpeg" "$build/empty-libs"
tar -xJf "$deps/downloads/ffmpeg-$FFMPEG_VER.tar.xz" -C "$build/ffmpeg" --strip-components=1
"$sdk/bin/prospero-ar" rc "$build/empty-libs/libm.a"
cat > "$build/dav1d.pc" <<EOF
prefix=$prefix
Name: dav1d
Description: dav1d
Version: $DAV1D_VER
Libs: -L$prefix/lib -ldav1d
Cflags: -I$prefix/include
EOF
(cd "$build/ffmpeg" && PKG_CONFIG_LIBDIR="$build" ./configure --prefix="$prefix" \
    --enable-cross-compile --target-os=freebsd --arch=x86_64 --cpu=znver2 \
    --cc="$sdk/bin/prospero-clang" --cxx="$sdk/bin/prospero-clang++" --ar="$sdk/bin/prospero-ar" \
    --ranlib="$sdk/bin/prospero-ranlib" --nm="$sdk/bin/prospero-nm" --x86asmexe="$nasm" \
    --host-cc="$PS5_CLANG" \
    --pkg-config=pkg-config --pkg-config-flags=--static \
    --enable-static --disable-shared --enable-pic --disable-programs --disable-doc --disable-debug \
    --disable-autodetect --disable-avdevice --disable-avfilter --disable-avformat --disable-swresample \
    --disable-network --disable-everything --enable-libdav1d \
    --enable-decoder=libdav1d --enable-decoder=vp8 --enable-decoder=vp9 --enable-parser=vp9 --enable-parser=av1 \
    --extra-cflags="-fno-omit-frame-pointer" --extra-ldflags="-L$build/empty-libs" > "$build/configure.log" 2>&1) ||
    { tail -30 "$build/configure.log" >&2; tail -40 "$build/ffmpeg/ffbuild/config.log" >&2; exit 1; }
make -C "$build/ffmpeg" -j"$(nproc)" > "$build/make.log" 2>&1 || { tail -30 "$build/make.log" >&2; exit 1; }
make -C "$build/ffmpeg" install > "$build/install.log" 2>&1
printf '%s\n' "$stamp" > "$prefix/.stamp"
echo "ffmpeg: $FFMPEG_VER with dav1d $DAV1D_VER in $prefix ($(du -sh "$prefix/lib" | cut -f1))"
