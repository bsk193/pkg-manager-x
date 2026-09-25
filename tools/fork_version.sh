#!/usr/bin/env bash
# PKG Manager X version helper.
#
#   tools/fork_version.sh upstream        upstream PKGMGR_VERSION (include/version.h), e.g. 1.2.4
#   tools/fork_version.sh dev             version for non-release builds, from `git describe`
#                                         (e.g. 1.1.0-3-gabc1234, or 0.0.0-dev without tags)
#   tools/fork_version.sh parse <tag>     validates a release tag and prints
#                                         "<version> <release|prerelease>"
#
# Release tags:  x-vMAJOR.MINOR.PATCH                 -> release (marked Latest)
#                x-vMAJOR.MINOR.PATCH-(alpha|beta|rc).N -> pre-release
set -euo pipefail
cd "$(dirname "$0")/.."

TAG_RE='^x-v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(alpha|beta|rc)\.(0|[1-9][0-9]*))?$'

case "${1:-}" in
    upstream)
        UP=$(sed -n 's/^#define PKGMGR_VERSION "\(.*\)".*/\1/p' include/version.h | tr -d '\r')
        [ -n "$UP" ] || { echo "PKGMGR_VERSION not found in include/version.h" >&2; exit 1; }
        echo "$UP"
        ;;
    dev)
        DESC=$(git describe --tags --match 'x-v*' 2>/dev/null || true)
        if [ -n "$DESC" ]; then echo "${DESC#x-v}"; else echo "0.0.0-dev"; fi
        ;;
    parse)
        TAG="${2:-}"
        if ! [[ "$TAG" =~ $TAG_RE ]]; then
            echo "Invalid release tag '$TAG': use x-v1.2.3 or x-v1.2.3-beta.1 (alpha/beta/rc)" >&2
            exit 1
        fi
        VER="${TAG#x-v}"
        if [[ "$VER" == *-* ]]; then echo "$VER prerelease"; else echo "$VER release"; fi
        ;;
    *)
        echo "usage: $0 upstream | dev | parse <tag>" >&2
        exit 1
        ;;
esac
