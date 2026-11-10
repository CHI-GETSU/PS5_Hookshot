#!/usr/bin/env bash
# PS5: upstream zstd's decompressor (BSD), static, for the console: the .nup keeps some romfs files as zstd frames
# (host/ps5/nup_romfs.c). Not third_party/zbic's copy: that one carries Atmosphere's GPLv2-only ZBIC code.
#
#   ps5/tools/build-zstd.sh            # into .local/ps5-deps/zstd
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
deps="$root/.local/ps5-deps"
ZSTD_VER=1.5.7
BUILD_TAG="$ZSTD_VER-notrace"
prefix="$deps/zstd"
[[ -f $prefix/lib/libzstd.a && -f $prefix/.version && $(<"$prefix/.version") == "$BUILD_TAG" ]] && { echo "zstd: built already"; exit 0; }
sdk=${PS5_PAYLOAD_SDK:-$root/../PS5_VulkanTemplate/.deps/native/ps5-payload-sdk}
export PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=${PS5_CLANG:-$(command -v clang)}
mkdir -p "$deps/downloads"
archive="$deps/downloads/zstd-$ZSTD_VER.tar.gz"
[[ -f $archive ]] || curl -fL --retry 3 --proto '=https' -o "$archive" \
    "https://github.com/facebook/zstd/releases/download/v$ZSTD_VER/zstd-$ZSTD_VER.tar.gz"
build="$deps/build-zstd"
rm -rf -- "$build" && mkdir -p "$build"
tar -xzf "$archive" -C "$build" --strip-components=1
make -C "$build/lib" -j"$(nproc)" libzstd.a CC="$sdk/bin/prospero-clang" AR="$sdk/bin/prospero-ar" \
    CFLAGS="-O2 -march=znver2 -fno-omit-frame-pointer -DZSTD_NO_TRACE" ZSTD_LIB_COMPRESSION=0 ZSTD_LIB_DICTBUILDER=0 \
    ZSTD_LIB_DEPRECATED=0 ZSTD_NO_ASM=1 > "$build/make.log" 2>&1 || { tail -20 "$build/make.log" >&2; exit 1; }
mkdir -p "$prefix/lib" "$prefix/include"
cp "$build/lib/libzstd.a" "$prefix/lib/"
cp "$build/lib/zstd.h" "$build/lib/zstd_errors.h" "$prefix/include/"
echo "$BUILD_TAG" > "$prefix/.version"
echo "zstd: $ZSTD_VER (decompression) in $prefix"
