#ifndef PKG_PLATFORM_H
#define PKG_PLATFORM_H

/*
 * PKG Manager X - package target platform ("ps4" / "ps5"), content type
 * and install gating. Everything is derived from package metadata, never
 * from the folder a package is stored in.
 *
 * Platform (first hit wins):
 *   1. Parser evidence: \x7fFIH container or an embedded param.json => ps5,
 *      a CNT package that only carries param.sfo => ps4.
 *   2. Title ID prefix of a retail title (PPSA... => ps5, CUSA... => ps4),
 *      for multi-part containers and old cache entries.
 *
 * Content type: "game" / "dlc" / "update" from the param category
 * (gd / ac / gp), "homebrew" for applications that are not retail titles
 * (content ID publisher IV0000, or a title ID outside the retail prefixes).
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

/* Fills d->platform from the title ID when parser evidence was missing
 * (multi-part containers, old cache entries). Idempotent. */
void pkg_platform_finalize(pkg_detail_t *d);

/* "ps4" / "ps5" for a retail Title ID prefix, else "". */
const char *pkg_platform_from_title_id(const char *title_id);

/* "ps4" / "ps5" when a single folder name is a platform folder ("PS4",
 * "ps5", "PS4 Games", "PS5_pkgs"), else "". Only used to decide which
 * folders on USB drives are scanned, never to classify packages. */
const char *pkg_platform_folder_name(const char *name);

/* 1 for applications that are not retail titles (see above). */
int pkg_platform_is_homebrew(const pkg_detail_t *d);

/* "game" / "dlc" / "update" / "homebrew". */
const char *pkg_platform_content_type(const pkg_detail_t *d);

/* Console this build runs on: "ps5" or "ps4". Host builds read
 * PKGMGR_CONSOLE (default "ps5") so tests can emulate either console. */
const char *pkg_platform_console(void);

/* Settings switch "Allow PS4 installs on PS5" (default on). */
void pkg_platform_set_allow_ps4_on_ps5(int allow);
int pkg_platform_get_allow_ps4_on_ps5(void);

/* 1 if the running console can install the package. When 0, *reason (if
 * non-NULL) points at a static, user-facing explanation (one of the
 * PKG_PLATFORM_REASON_* strings).
 *   PS4: PS4 packages only.
 *   PS5: PS5 packages; PS4 games / DLC / updates unless the settings switch
 *        is off; never PS4 homebrew.
 * Unknown platforms are allowed (the system installer has the final say). */
int pkg_platform_can_install(const pkg_detail_t *d, const char **reason);

#define PKG_PLATFORM_REASON_PS5_ONLY     "PS5 only"
#define PKG_PLATFORM_REASON_PS4_HOMEBREW "PS4 homebrew doesn't run on PS5"
#define PKG_PLATFORM_REASON_PS4_DISABLED "PS4 installs are turned off in Settings"

#ifdef __cplusplus
}
#endif

#endif /* PKG_PLATFORM_H */
