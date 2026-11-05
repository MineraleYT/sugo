#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds libsodium for macOS as one universal (arm64 + x86_64) static library, for the sync's
# encryption (SyncCrypto), and installs it with a pkg-config file where CMake finds it:
#
#   bash packaging/macos/build-libsodium.sh "$HOME/sugo-deps/sodium"
#   export PKG_CONFIG_PATH="$HOME/sugo-deps/sodium/lib/pkgconfig"
#
# Homebrew's libsodium is a dylib for the Mac's own architecture only: enough to develop, not for
# the universal app. A static library is linked into the program, so the bundle carries nothing
# more (build.sh checks that no libsodium dylib is linked). The source tarball is pinned by
# version and SHA-256 (the one signed by libsodium's minisign key, checked when the pin changed).
# Nothing is done when PREFIX already holds this version.
set -euo pipefail

PREFIX=${1:?installation prefix}
LIBSODIUM_VERSION=1.0.22
LIBSODIUM_SHA256=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-13.0}
ARCHS="arm64 x86_64"

fail() { echo "error: $*" >&2; exit 1; }

stamp="$PREFIX/.sugo-libsodium-$LIBSODIUM_VERSION-$DEPLOYMENT_TARGET"
if [ -e "$stamp" ] && [ -e "$PREFIX/lib/libsodium.a" ] && [ -e "$PREFIX/lib/pkgconfig/libsodium.pc" ]; then
    exit 0
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/sugo-libsodium.XXXXXX")
trap 'rm -rf "$work"' EXIT
tarball="$work/libsodium-$LIBSODIUM_VERSION.tar.gz"
curl -fsSL --retry 3 -o "$tarball" \
    "https://download.libsodium.org/libsodium/releases/libsodium-$LIBSODIUM_VERSION.tar.gz" \
    || curl -fsSL --retry 3 -o "$tarball" \
        "https://github.com/jedisct1/libsodium/releases/download/$LIBSODIUM_VERSION-RELEASE/libsodium-$LIBSODIUM_VERSION.tar.gz" \
    || fail "cannot download libsodium $LIBSODIUM_VERSION"
actual=$(shasum -a 256 "$tarball" | cut -d' ' -f1)
[ "$actual" = "$LIBSODIUM_SHA256" ] || fail "libsodium $LIBSODIUM_VERSION: SHA-256 $actual, expected $LIBSODIUM_SHA256"

jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
# One build per architecture (configure detects the CPU features of one target at a time), as
# libsodium's own dist-build/apple-xcframework.sh does; --host makes either a cross build, so
# nothing built is run and an Intel or Apple Silicon Mac gives the same result.
for arch in $ARCHS; do
    case "$arch" in
        arm64) host=aarch64-apple-darwin ;;
        x86_64) host=x86_64-apple-darwin ;;
    esac
    mkdir -p "$work/src-$arch"
    tar -xzf "$tarball" -C "$work/src-$arch" --strip-components 1
    (
        cd "$work/src-$arch"
        export CFLAGS="-O2 -arch $arch -mmacosx-version-min=$DEPLOYMENT_TARGET"
        export LDFLAGS="-arch $arch -mmacosx-version-min=$DEPLOYMENT_TARGET"
        ./configure --host="$host" --prefix="$work/$arch" --disable-shared --enable-static \
            --disable-dependency-tracking >"$work/configure-$arch.log" 2>&1 \
            || { tail -30 "$work/configure-$arch.log"; exit 1; }
        make -j"$jobs" install >"$work/make-$arch.log" 2>&1 || { tail -30 "$work/make-$arch.log"; exit 1; }
    ) || fail "libsodium did not build for $arch"
done

# The headers do not depend on the architecture: install those of the first build, after
# checking that the other agrees.
first=${ARCHS%% *}
for arch in $ARCHS; do
    diff -r "$work/$first/include" "$work/$arch/include" >/dev/null \
        || fail "libsodium's headers differ between $first and $arch"
done

rm -rf "$PREFIX"
mkdir -p "$PREFIX/lib/pkgconfig"
cp -R "$work/$first/include" "$PREFIX/include"
libs=()
for arch in $ARCHS; do libs+=("$work/$arch/lib/libsodium.a"); done
lipo -create "${libs[@]}" -output "$PREFIX/lib/libsodium.a"
for arch in $ARCHS; do
    lipo "$PREFIX/lib/libsodium.a" -verify_arch "$arch" || fail "libsodium.a has no $arch"
done
# The pkg-config file of the first build, pointing at PREFIX.
sed -e "s|^prefix=.*|prefix=$PREFIX|" "$work/$first/lib/pkgconfig/libsodium.pc" > "$PREFIX/lib/pkgconfig/libsodium.pc"
touch "$stamp"
echo "libsodium $LIBSODIUM_VERSION ($(lipo -archs "$PREFIX/lib/libsodium.a"), static) in $PREFIX"
