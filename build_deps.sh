#!/usr/bin/env bash
# Dependency build script for the PS5 / PS4 Payload SDKs inside Docker.
#   PLATFORM=ps5 (default) -> /opt/ps5-payload-sdk, prospero-* tools
#   PLATFORM=ps4           -> /opt/ps4-payload-sdk, orbis-* tools
# libmicrohttpd 1.0.1 and mbedTLS 3.6.2 are pinned. libsmb2 tracks upstream
# master shallowly; set LIBSMB2_REF to a commit SHA to pin (see Dockerfile.sdk).
# NOTE: Makefile links deps/libsmb2/build* (local) when the SDK target copy
# is missing. Keep both in sync when bumping libsmb2.
set -euo pipefail

PLATFORM="${PLATFORM:-ps5}"
case "$PLATFORM" in
    ps5) SDK=/opt/ps5-payload-sdk; PFX=prospero ;;
    ps4) SDK=/opt/ps4-payload-sdk; PFX=orbis ;;
    *) echo "PLATFORM must be ps5 or ps4" >&2; exit 1 ;;
esac

export PATH="$SDK/bin:$PATH"

TEMPDIR=$(mktemp -d)
trap 'rm -rf -- "$TEMPDIR"' EXIT

cd "$TEMPDIR"

# Map tools to SDK wrappers
export CC=$PFX-clang
export CXX=$PFX-clang++
export AR=$PFX-ar
export NM=$PFX-nm
export RANLIB=$PFX-ranlib

echo "=== Building libmicrohttpd 1.0.1 for $PLATFORM ==="
wget -O libmicrohttpd.tar.gz https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz
tar xf libmicrohttpd.tar.gz
cd libmicrohttpd-1.0.1
./configure --host=x86_64-pc-freebsd12 \
            --disable-shared --enable-static \
            --disable-curl --disable-examples \
            --prefix=$SDK/target
make -j$(nproc)
make install
cd "$TEMPDIR"

echo "=== Building libsmb2 for $PLATFORM ==="
git clone --depth 1 https://github.com/sahlberg/libsmb2.git libsmb2-src
cd libsmb2-src
mkdir build && cd build
$PFX-cmake .. -DBUILD_SHARED_LIBS=OFF \
              -DCMAKE_INSTALL_PREFIX=$SDK/target
make -j$(nproc)
make install
cd "$TEMPDIR"

echo "=== Building mbedTLS 3.6.2 for $PLATFORM (HTTPS sources) ==="
wget -O mbedtls.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
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
$PFX-cmake .. -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
              -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
              -DCMAKE_INSTALL_PREFIX=$SDK/target
make -j$(nproc)
make install

echo "libmicrohttpd, libsmb2 and mbedTLS successfully built and installed into $SDK/target!"
