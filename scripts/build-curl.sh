#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds libcurl for the Switch the way devkitPro's switch-curl package does
# (pacman-packages, switch/curl/PKGBUILD), plus patches/curl/dreadovania-libnx.patch
# (ADR 0015). Run in the pinned devkitPro container; `make` runs it when needed.
# Everything downloaded is checked against a pinned SHA-256.
set -euo pipefail

CURL_VERSION=7.69.1
CURL_URL="https://github.com/curl/curl/releases/download/curl-7_69_1/curl-${CURL_VERSION}.tar.xz"
CURL_SHA256=03c7d5e6697f7b7e40ada1b2256e565a555657398e6c1fcfa4cb251ccd819d4f
# devkitPro's patch, as their PKGBUILD (switch-curl 7.69.1-5) pins it.
DKP_PATCH_URL="https://raw.githubusercontent.com/devkitPro/pacman-packages/master/switch/curl/switch-curl.patch"
DKP_PATCH_SHA256=723c7d884fc7c39ae1a3115ba245bb8c1415da47bbd60ab8f943ca98f92ebc9a

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build-curl"
prefix="$out/prefix"
our_patch="$root/patches/curl/dreadovania-libnx.patch"
stamp="$CURL_SHA256 $DKP_PATCH_SHA256 $(sha256sum "$our_patch" | cut -d' ' -f1)"

if [ -f "$prefix/lib/libcurl.a" ] && [ "$(cat "$out/stamp" 2>/dev/null)" = "$stamp" ]; then
    exit 0  # already built from these sources
fi

if ! command -v autoreconf >/dev/null || ! command -v libtoolize >/dev/null; then
    if [ "$(id -u)" = 0 ]; then
        apt-get update -qq && apt-get install -y -qq --no-install-recommends autoconf automake libtool >/dev/null
    else
        echo "build-curl.sh needs autoconf, automake and libtool. Run once as root in the" >&2
        echo "devkitPro container (without -u), or install them there." >&2
        exit 1
    fi
fi

rm -rf "$out" && mkdir -p "$out/download"
fetch() {  # url, sha256, file
    curl -fsSL --retry 3 -o "$out/download/$3" "$1"
    echo "$2  $out/download/$3" | sha256sum -c --quiet -
}
fetch "$CURL_URL" "$CURL_SHA256" "curl-${CURL_VERSION}.tar.xz"
fetch "$DKP_PATCH_URL" "$DKP_PATCH_SHA256" switch-curl.patch

tar -xf "$out/download/curl-${CURL_VERSION}.tar.xz" -C "$out"
cd "$out/curl-${CURL_VERSION}"
patch -Np1 -i "$out/download/switch-curl.patch" >/dev/null
patch -Np1 -i "$our_patch" >/dev/null

# As devkitPro's PKGBUILD, with the prefix inside the build folder.
set +u
# shellcheck disable=SC1091
source /opt/devkitpro/switchvars.sh
set -u
LDFLAGS="-specs=${DEVKITPRO}/libnx/switch.specs ${LDFLAGS}"
export LDFLAGS
./buildconf >/dev/null
./configure --prefix="$prefix" --host=aarch64-none-elf \
    --disable-shared --enable-static --disable-ipv6 --disable-unix-sockets \
    --disable-manual --disable-ntlm-wb --disable-threaded-resolver \
    --without-ssl --without-polar-ssl --without-cyassl --without-wolfssl \
    --without-mbedtls \
    --with-libnx \
    --with-default-ssl-backend=libnx >/dev/null
make -s -C lib -j "$(nproc)" >/dev/null
make -s -C lib install >/dev/null
make -s -C include install >/dev/null
echo "$stamp" > "$out/stamp"
echo "built libcurl ${CURL_VERSION} (devkitPro patch + ours) in $prefix"
