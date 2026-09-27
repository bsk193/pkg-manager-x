#ifndef PS4_NOTIFY_H
#define PS4_NOTIFY_H

/*
 * PKG Manager X - PS4-only notification and user-service policy.
 *
 * Pure helpers (no system calls), so host tests cover them. The PS4 build
 * calls them from PKGMGR_CONSOLE_PS4 branches; PS5 code paths never use them.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Home screen tile package (ps4-launcher/). */
#define PKGMGR_TILE_TITLE_ID "PKGX00001"

/* SCE_USER_SERVICE_ERROR_ALREADY_INITIALIZED (OpenOrbis
 * include/orbis/_types/errors.h): the process hosting the payload already
 * initialized the user service. It is usable as is and must not be reset or
 * terminated by us. */
#define PKGMGR_USER_SERVICE_ERROR_ALREADY_INITIALIZED ((int)0x80960003)

typedef enum {
    PS4_USER_SERVICE_FAILED = -1,             /* genuine error: report it */
    PS4_USER_SERVICE_READY = 0,               /* initialized by us */
    PS4_USER_SERVICE_ALREADY_INITIALIZED = 1  /* owned elsewhere, usable */
} ps4_user_service_state_t;

/* Classifies a sceUserServiceInitialize() result. Only 0 and
 * ALREADY_INITIALIZED count as usable; every other code is an error. */
ps4_user_service_state_t ps4_user_service_classify(int rc);

/* Startup scan popup, exactly two lines:
 *   PKG Manager X v<version>
 *   Found <count> packages
 * Connection details (IP / port) go to the web UI and logs instead.
 * Returns the snprintf result. */
int ps4_format_scan_notification(char *out, size_t out_sz, const char *version, int count);

/* 1 when title_id is the PKG Manager X tile (case-insensitive). */
int ps4_is_manager_tile(const char *title_id);

/* 1 when the manager's own "Installing ..." / "... is ready to play!" popups
 * are skipped: installing the PKG Manager X tile on a PS4 (the PS4's own
 * download notifications still appear). console is pkg_platform_console(). */
int ps4_suppress_install_notification(const char *console, const char *title_id);

#ifdef __cplusplus
}
#endif

#endif /* PS4_NOTIFY_H */
