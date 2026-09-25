#!/bin/bash
# PKG Manager X - Versioned Build Script
# Builds the frontend once, then one ELF per console inside its SDK image.
#   ./build_release.sh                    PS5 + PS4, version from `git describe` (x-v* tags)
#   ./build_release.sh ps5                PS5 only
#   X_VERSION=1.1.0 ./build_release.sh    explicit version (CI takes it from the x-v1.1.0 tag)
# Output: pkg-manager-x_v<version>[-<hash>]_<ps5|ps4>.elf
# WORKDIR is /src (see Dockerfile.sdk*); mount with -v "$(pwd)":/src -w /src.
set -euo pipefail

TARGETS="${*:-ps5 ps4}"

SHORT_HASH=$(git rev-parse --short HEAD 2>/dev/null || true)
if [ -z "$SHORT_HASH" ]; then
    echo "Error: Could not determine the current Git commit"
    exit 1
fi

if [ -n "${X_VERSION:-}" ]; then
    VERSION="$X_VERSION"
    SUFFIX=""
else
    VERSION=$(bash tools/fork_version.sh dev)
    SUFFIX="-${SHORT_HASH}"
fi
export X_VERSION="$VERSION"

UPSTREAM=$(bash tools/fork_version.sh upstream)
echo "--- Building PKG Manager X v$VERSION, based on PKG Manager v$UPSTREAM ($TARGETS) ---"

# 1. Build React Frontend (on Host)
echo "[1/3] Building React Frontend..."
make frontend-build X_VERSION="$VERSION"
echo "      Frontend build successful."

for PLATFORM in $TARGETS; do
    case "$PLATFORM" in
        ps5) IMAGE_NAME="ps5-payload-sdk-pkgmgr"; DOCKERFILE="Dockerfile.sdk"; ELF="pkgmgr.elf" ;;
        ps4) IMAGE_NAME="ps4-payload-sdk-pkgmgr"; DOCKERFILE="Dockerfile.sdk-ps4"; ELF="pkgmgr-ps4.elf" ;;
        *) echo "Unknown target '$PLATFORM' (use ps5 / ps4)"; exit 1 ;;
    esac
    OUTPUT_ELF="pkg-manager-x_v${VERSION}${SUFFIX}_${PLATFORM}.elf"

    # 2. Build/verify the docker image
    if [[ "$(docker images -q $IMAGE_NAME 2> /dev/null)" == "" ]]; then
        echo "      Docker image $IMAGE_NAME not found. Building... (this may take a few minutes)"
        if ! docker build -t $IMAGE_NAME -f $DOCKERFILE .; then
            echo "      !!! Docker image build FAILED!"
            exit 1
        fi
        echo "      Docker image built successfully."
    fi

    # 3. Build native ELF via Docker (commit passed in: git inside the
    #    container may refuse the bind-mounted repo as "dubious ownership").
    echo "[2/3] Building native $PLATFORM ELF via Docker..."
    rm -f "$ELF"
    docker run --rm -v "$(pwd)":/src -w /src $IMAGE_NAME \
        make all PLATFORM=$PLATFORM X_VERSION="$VERSION" BUILD_COMMIT="$SHORT_HASH"

    # 4. Rename output
    if [ -f "$ELF" ]; then
        mv "$ELF" "$OUTPUT_ELF"
        echo "[3/3] Created versioned binary: $OUTPUT_ELF"
    else
        echo "      !!! $ELF not found after build!"
        exit 1
    fi
done

echo "--- Build Complete! ---"
