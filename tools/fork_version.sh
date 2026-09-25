#!/usr/bin/env bash
# PKG Manager X version helper.
#
#   tools/fork_version.sh            next release version   e.g. 1.2.4-x3
#   tools/fork_version.sh current    latest released version (or "" if none)
#   tools/fork_version.sh dev        local/CI development version  1.2.4-x-dev
#   tools/fork_version.sh upstream   upstream PKGMGR_VERSION      1.2.4
#
# Scheme: <upstream>-x<N>. N counts fork releases on top of the upstream
# version in include/version.h and restarts at 1 after merging a new upstream
# release. Release tags are "v<version>" (v1.2.4-x3). Needs the tags locally
# (git fetch --tags / actions/checkout fetch-depth: 0).
set -euo pipefail
cd "$(dirname "$0")/.."

UP=$(sed -n 's/^#define PKGMGR_VERSION "\(.*\)".*/\1/p' include/version.h | tr -d '\r')
if [ -z "$UP" ]; then
    echo "PKGMGR_VERSION not found in include/version.h" >&2
    exit 1
fi

last_rev() {
    local up_re="${UP//./\\.}"
    git tag -l "v${UP}-x*" | sed -n "s/^v${up_re}-x\([0-9][0-9]*\)$/\1/p" | sort -n | tail -1
}

case "${1:-next}" in
    next)     LAST=$(last_rev); echo "${UP}-x$(( ${LAST:-0} + 1 ))" ;;
    current)  LAST=$(last_rev); [ -n "$LAST" ] && echo "${UP}-x${LAST}" || echo "" ;;
    dev)      echo "${UP}-x-dev" ;;
    upstream) echo "$UP" ;;
    *) echo "usage: $0 [next|current|dev|upstream]" >&2; exit 1 ;;
esac
