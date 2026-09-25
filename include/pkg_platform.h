#ifndef PKG_PLATFORM_H
#define PKG_PLATFORM_H

/*
 * PKG Manager X - package target platform ("ps4" / "ps5") detection and
 * install gating.
 *
 * Detection order (first hit wins):
 *   1. Parser evidence: \x7fFIH container or an embedded param.json => ps5,
 *      a CNT package that only carries param.sfo => ps4.
 *   2. Title ID prefix: PPSA => ps5, CUSA => ps4.
 *   3. Folder hint: a path component named PS4/PS5 (e.g. /mnt/usb0/PS4/...).
 * The folder hint is also reported separately so the UI can flag packages
 * stored in the "wrong" folder.
 */

#include <stddef.h>
#include <stdint.h>
#include "pkg_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parser hooks. Safe to call with NULL. */
void pkg_platform_note_header(pkg_detail_t *d, const uint8_t *hdr, size_t hdr_len);
void pkg_platform_note_param_json(pkg_detail_t *d);
void pkg_platform_note_param_sfo(pkg_detail_t *d);

/* Fills d->platform from title ID / folder hint when parser evidence was
 * missing (multi-part containers, old cache entries). Idempotent. */
void pkg_platform_finalize(pkg_detail_t *d);

/* "ps4" / "ps5" for a known Title ID prefix, else "". */
const char *pkg_platform_from_title_id(const char *title_id);

/* "ps4" / "ps5" if any path component is a PS4/PS5 folder, else "". */
const char *pkg_platform_folder_hint(const char *path);

/* "ps4" / "ps5" when a single folder name is a platform folder ("PS4",
 * "ps5", "PS4 Games", "PS5_pkgs"), else "". */
const char *pkg_platform_folder_name(const char *name);

/* 1 when the folder hint and the detected platform disagree. */
int pkg_platform_folder_mismatch(const pkg_detail_t *d);

/* Console this build runs on: "ps5" or "ps4". Host builds read
 * PKGMGR_CONSOLE (default "ps5") so tests can emulate either console. */
const char *pkg_platform_console(void);

/* 1 if the running console can install the package. When 0, *reason (if
 * non-NULL) points at a static, user-facing explanation. PS5 installs PS4
 * and PS5 packages; PS4 installs PS4 packages only. Unknown platforms are
 * allowed (the system installer has the final say). */
int pkg_platform_can_install(const pkg_detail_t *d, const char **reason);

#define PKG_PLATFORM_REASON_PS5_ON_PS4 "PS5 package cannot be installed on PS4"

#ifdef __cplusplus
}
#endif

#endif /* PKG_PLATFORM_H */
