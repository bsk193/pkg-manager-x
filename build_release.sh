#!/bin/bash
# PKG Manager X - Versioned Build Script
# Builds the frontend once, then one ELF per console inside its SDK image.
#   ./build_release.sh            PS5 + PS4
#   ./build_release.sh ps5        PS5 only
#   ./build_release.sh ps4        PS4 only
# WORKDIR is /src (see Dockerfile.sdk*); mount with -v "$(pwd)":/src -w /src.
set -euo pipefail

TARGETS="${*:-ps5 ps4}"

# 1. Extract version from include/version.h
VERSION=$(grep '#define PKGMGR_VERSION' include/version.h | awk '{print $3}' | tr -d '"' | tr -d '\r')

if [ -z "$VERSION" ]; then
    echo "Error: Could not find PKGMGR_VERSION in include/version.h"
    exit 1
fi

SHORT_HASH=$(git rev-parse --short HEAD 2>/dev/null || true)
if [ -z "$SHORT_HASH" ]; then
    echo "Error: Could not determine the current Git commit"
    exit 1
fi

echo "--- Building PKG Manager X v$VERSION ($TARGETS) ---"

# 2. Build React Frontend (on Host)
echo "[1/3] Building React Frontend..."
make frontend-build
echo "      Frontend build successful."

for PLATFORM in $TARGETS; do
    case "$PLATFORM" in
        ps5) IMAGE_NAME="ps5-payload-sdk-pkgmgr"; DOCKERFILE="Dockerfile.sdk"; ELF="pkgmgr.elf" ;;
        ps4) IMAGE_NAME="ps4-payload-sdk-pkgmgr"; DOCKERFILE="Dockerfile.sdk-ps4"; ELF="pkgmgr-ps4.elf" ;;
        *) echo "Unknown target '$PLATFORM' (use ps5 / ps4)"; exit 1 ;;
    esac
    OUTPUT_ELF="pkg-manager-x_v${VERSION}-dev-${SHORT_HASH}_${PLATFORM}.elf"

    # 3. Build/verify the docker image
    if [[ "$(docker images -q $IMAGE_NAME 2> /dev/null)" == "" ]]; then
        echo "      Docker image $IMAGE_NAME not found. Building... (this may take a few minutes)"
        if ! docker build -t $IMAGE_NAME -f $DOCKERFILE .; then
            echo "      !!! Docker image build FAILED!"
            exit 1
        fi
        echo "      Docker image built successfully."
    fi

    # 4. Build native ELF via Docker
    echo "[2/3] Building native $PLATFORM ELF via Docker..."
    docker run --rm -v "$(pwd)":/src -w /src $IMAGE_NAME make clean all PLATFORM=$PLATFORM

    # 5. Rename output
    if [ -f "$ELF" ]; then
        mv "$ELF" "$OUTPUT_ELF"
        echo "[3/3] Created versioned binary: $OUTPUT_ELF"
    else
        echo "      !!! $ELF not found after build!"
        exit 1
    fi
done

echo "--- Build Complete! ---"
