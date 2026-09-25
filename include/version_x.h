#ifndef VERSION_X_H
#define VERSION_X_H

/*
 * PKG Manager X versions.
 *
 *   PKGMGR_X_VERSION         our own semver: "1.1.0", "1.1.1-beta.1"
 *   PKGMGR_UPSTREAM_VERSION  the PKG Manager release this fork is based on
 *
 * version.h (PKGMGR_VERSION) stays upstream's and is never edited here, so
 * merging upstream releases never conflicts on the version line; the
 * "based on" version follows the merge automatically.
 *
 * Releases are cut by pushing a tag "x-v<version>" (see
 * .github/workflows/release.yml); CI compiles the version in with
 * `make X_VERSION=...`. Local builds use `git describe` or "0.0.0-dev".
 */

#include "version.h"

#ifndef PKGMGR_X_VERSION
#define PKGMGR_X_VERSION "0.0.0-dev"
#endif

#define PKGMGR_UPSTREAM_VERSION PKGMGR_VERSION

#endif /* VERSION_X_H */
