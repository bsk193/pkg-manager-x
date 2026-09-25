#ifndef VERSION_X_H
#define VERSION_X_H

/*
 * PKG Manager X version: "<upstream version>-x<N>", e.g. "1.2.4-x3".
 *
 *   - version.h (PKGMGR_VERSION) stays upstream's and is never edited here,
 *     so merging upstream releases never conflicts on the version line.
 *   - N counts fork releases on top of that upstream version and restarts
 *     at 1 when upstream bumps (1.2.4-x3 -> merge 1.2.5 -> 1.2.5-x1).
 *   - Release builds get the exact string from CI (tools/fork_version.sh,
 *     Makefile X_VERSION=...); local builds report "<upstream>-x-dev".
 */

#include "version.h"

#ifndef PKGMGR_X_VERSION
#define PKGMGR_X_VERSION PKGMGR_VERSION "-x-dev"
#endif

#endif /* VERSION_X_H */
