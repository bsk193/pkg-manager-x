#!/usr/bin/env bash
# Dependency build script for PS5 Payload SDK inside Docker
# libmicrohttpd 1.0.1 is pinned (only pinned dep). libsmb2 tracks upstream
# master shallowly; set LIBSMB2_REF to a commit SHA to pin (see Dockerfile.sdk).
# NOTE: Makefile links deps/libsmb2/build (local); this script warms the SDK
# target copy. Keep both in sync when bumping libsmb2.
set -euo pipefail

export PATH="/opt/ps5-payload-sdk/bin:$PATH"

TEMPDIR=$(mktemp -d)
trap 'rm -rf -- "$TEMPDIR"' EXIT

cd "$TEMPDIR"

# Map tools to SDK prospero wrappers
export CC=prospero-clang
export CXX=prospero-clang++
export AR=prospero-ar
export NM=prospero-nm
export RANLIB=prospero-ranlib

echo "=== Building libmicrohttpd 1.0.1 for PS5 ==="
wget -O libmicrohttpd.tar.gz https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-1.0.1.tar.gz
tar xf libmicrohttpd.tar.gz
cd libmicrohttpd-1.0.1
./configure --host=x86_64-pc-freebsd12 \
            --disable-shared --enable-static \
            --disable-curl --disable-examples \
            --prefix=/opt/ps5-payload-sdk/target
make -j$(nproc)
make install

echo "=== Building libsmb2 for PS5 ==="
git clone --depth 1 https://github.com/sahlberg/libsmb2.git libsmb2-src
cd libsmb2-src
mkdir build && cd build
prospero-cmake .. -DBUILD_SHARED_LIBS=OFF \
                  -DCMAKE_INSTALL_PREFIX=/opt/ps5-payload-sdk/target
make -j$(nproc)
make install

echo "libmicrohttpd and libsmb2 successfully built and installed into SDK target!"
