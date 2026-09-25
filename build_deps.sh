#!/usr/bin/env bash
# Dependency build script for the PS5 / PS4 Payload SDKs inside Docker.
#   PLATFORM=ps5 (default) -> /opt/ps5-payload-sdk, prospero-* tools,
#                             installed into /opt/ps5-payload-sdk/target
#   PLATFORM=ps4           -> /opt/ps4-payload-sdk, toolchain/orbis.sh (same
#                             conventions as ps4-payload-dev/pacbrew), installed
#                             into /opt/ps4-payload-sdk/target/user/homebrew
# The Makefile's DEPS_ROOT points at the matching install root.
# libmicrohttpd 1.0.1 and mbedTLS 3.6.2 are pinned. libsmb2 tracks upstream
# master shallowly; set LIBSMB2_REF to a commit SHA to pin (see Dockerfile.sdk).
set -euo pipefail

PLATFORM="${PLATFORM:-ps5}"
case "$PLATFORM" in
    ps5)
        SDK=/opt/ps5-payload-sdk
        export PATH="$SDK/bin:$PATH"
        export CC=prospero-clang CXX=prospero-clang++ AR=prospero-ar NM=prospero-nm RANLIB=prospero-ranlib
        CMAKE=prospero-cmake
        HOST=x86_64-pc-freebsd12
        PREFIX="$SDK/target"
        unset DESTDIR || true
        ;;
    ps4)
        SDK=/opt/ps4-payload-sdk
        # Sets CC/CXX/AR/..., CMAKE, DESTDIR=$SDK/target and PREFIX=/user/homebrew.
        # shellcheck disable=SC1091
        source "$SDK/toolchain/orbis.sh"
        export PATH="$SDK/bin:$PATH"
        HOST=x86_64-pc-freebsd
        # libsmb2 treats PS4 as OpenOrbis and includes <endian.h>; the FreeBSD 9
        # based SDK only has <sys/endian.h> (same be16toh/htole64/... macros).
        if [ ! -f "$SDK/target/include/endian.h" ]; then
            printf '#pragma once\n/* PKG Manager X shim: OpenOrbis-style <endian.h> */\n#include <sys/endian.h>\n' \
                > "$SDK/target/include/endian.h"
        fi
        ;;
    *) echo "PLATFORM must be ps5 or ps4" >&2; exit 1 ;;
esac
echo "=== $PLATFORM: CC=$CC HOST=$HOST PREFIX=$PREFIX DESTDIR=${DESTDIR:-} ==="

TEMPDIR=$(mktemp -d)
trap 'rm -rf -- "$TEMPDIR"' EXIT

cd "$TEMPDIR"

# Print config.log when configure fails (CI only shows stdout).
configure_or_log() {
    if ! ./configure "$@"; then
        echo "=== configure failed; tail of config.log ===" >&2
        tail -n 120 config.log >&2 || true
        exit 1
    fi
}

echo "=== Building libmicrohttpd 1.0.1 for $PLATFORM ==="
wget -q -O libmicrohttpd.tar.gz https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz
tar xf libmicrohttpd.tar.gz
cd libmicrohttpd-1.0.1
configure_or_log --host="$HOST" \
                 --disable-shared --enable-static \
                 --disable-curl --disable-examples --disable-doc \
                 --prefix="$PREFIX"
# Library + public header only: the bundled test tools (e.g. the CPU-affinity
# helper) use APIs missing from the PS4's FreeBSD 9 headers.
make -j"$(nproc)" -C src/microhttpd
make -C src/microhttpd install
make -C src/include install
cd "$TEMPDIR"

echo "=== Building libsmb2 for $PLATFORM ==="
git clone --depth 1 https://github.com/sahlberg/libsmb2.git libsmb2-src
cd libsmb2-src
mkdir build && cd build
$CMAKE .. -DBUILD_SHARED_LIBS=OFF \
          -DCMAKE_INSTALL_PREFIX="$PREFIX"
make -j"$(nproc)"
make install
cd "$TEMPDIR"

echo "=== Building mbedTLS 3.6.2 for $PLATFORM (HTTPS sources) ==="
wget -q -O mbedtls.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
tar xf mbedtls.tar.bz2
cd mbedtls-3.6.2
# Payload-friendly configuration:
#  - our own sockets (no net_sockets), no timing module
#  - TLS 1.2 only (TLS 1.3 would require PSA init before every handshake)
#  - entropy from pkgmgr (kern.arandom / /dev/urandom) via mbedtls_hardware_poll
#  - mbedtls_platform_zeroize from pkgmgr (payload libcs may lack explicit_bzero)
# Both hooks live in src/http_source.c.
python3 scripts/config.py unset MBEDTLS_NET_C
python3 scripts/config.py unset MBEDTLS_TIMING_C
python3 scripts/config.py unset MBEDTLS_SSL_PROTO_TLS1_3
python3 scripts/config.py set MBEDTLS_NO_PLATFORM_ENTROPY
python3 scripts/config.py set MBEDTLS_ENTROPY_HARDWARE_ALT
python3 scripts/config.py set MBEDTLS_PLATFORM_ZEROIZE_ALT
mkdir build && cd build
$CMAKE .. -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
          -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
          -DCMAKE_INSTALL_PREFIX="$PREFIX"
make -j"$(nproc)"
make install

echo "libmicrohttpd, libsmb2 and mbedTLS installed for $PLATFORM (${DESTDIR:-}${PREFIX})"
