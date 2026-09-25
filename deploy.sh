#!/bin/bash
# PKG Manager X - Automated Build & Deploy Script
#   ./deploy.sh <PS5_IP>            PS5 via elfldr (port 9021)
#   ./deploy.sh <PS4_IP> ps4        PS4 via GoldHEN payload loader (port 9090)
#   LOADER_PORT=9021 ./deploy.sh <PS4_IP> ps4   PS4 via elfldr instead

if [ -z "$1" ]; then
    echo "Usage: ./deploy.sh <CONSOLE_IP> [ps5|ps4]"
    exit 1
fi

CONSOLE_IP="$1"
PLATFORM="${2:-ps5}"
case "$PLATFORM" in
    ps5) ELF="pkgmgr.elf";     IMAGE_NAME="ps5-payload-sdk-pkgmgr"; LOADER_PORT="${LOADER_PORT:-9021}" ;;
    ps4) ELF="pkgmgr-ps4.elf"; IMAGE_NAME="ps4-payload-sdk-pkgmgr"; LOADER_PORT="${LOADER_PORT:-9090}" ;;
    *) echo "Platform must be ps5 or ps4"; exit 1 ;;
esac

echo "--- Deploying PKG Manager X ($PLATFORM) to $CONSOLE_IP ---"

# 1. Build the React Frontend
echo "[1/3] Building React Frontend..."
make frontend-build > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "      !!! Frontend build FAILED!"
    exit 1
fi
echo "      Frontend build successful."

# 2. Build native ELF via Docker (keep output visible; it contains the
# helper/loader compile lines needed to confirm what actually rebuilt)
echo "[2/3] Building native ELF via Docker..."
# PS5: force embedded-helper regeneration regardless of mtimes: blob.S incbins
# build/install-helper.elf, so a stale helper silently ships old behavior.
[ "$PLATFORM" = ps5 ] && rm -f build/install-helper.elf
docker run --rm -v "$(pwd)":/src -w /src $IMAGE_NAME make clean all PLATFORM=$PLATFORM 2>&1 | tee build-docker.log

if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "      !!! ELF build FAILED! See build-docker.log."
    exit 1
fi
echo "      ELF build successful."

# 2b. PS5: verify the embedded helper was actually regenerated.
if [ "$PLATFORM" = ps5 ]; then
    if [ ! -f "build/install-helper.elf" ]; then
        echo "      !!! build/install-helper.elf missing after build! See build-docker.log."
        exit 1
    fi
    ls -l build/install-helper.elf
    python3 - <<'EOF'
d = open('build/install-helper.elf','rb').read()
h = 2166136261
for b in d:
    h = ((h ^ b) * 16777619) & 0xFFFFFFFF
print('      helper bytes=%d fnv1a=0x%08X (compare with runtime embedded_fnv1a)' % (len(d), h))
EOF
fi

# 3. Send to console
if [ -f "$ELF" ]; then
    echo "[3/3] Sending $ELF to $CONSOLE_IP:$LOADER_PORT via socat..."
    socat -u - TCP:$CONSOLE_IP:$LOADER_PORT < "$ELF"
    if [ $? -eq 0 ]; then
        echo "--- Deployment Complete! ---"
    else
        echo "      !!! Failed to send ELF. Is the payload loader running?"
        exit 1
    fi
else
    echo "      !!! $ELF not found!"
    exit 1
fi
