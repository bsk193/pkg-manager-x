#!/bin/bash
# Refreshes assets/cacert.pem (Mozilla's CA list as published by curl) that
# is compiled into the payload for HTTPS sources. Commit the result.
set -euo pipefail
cd "$(dirname "$0")/.."
curl -fsSL --proto '=https' https://curl.se/ca/cacert.pem -o assets/cacert.pem.new
count=$(grep -c 'BEGIN CERTIFICATE' assets/cacert.pem.new)
if [ "$count" -lt 100 ]; then
    echo "Downloaded bundle has only $count certificates; keeping the old one" >&2
    rm -f assets/cacert.pem.new
    exit 1
fi
mv assets/cacert.pem.new assets/cacert.pem
grep -m1 'Certificate data from Mozilla' assets/cacert.pem
echo "$count certificates"
