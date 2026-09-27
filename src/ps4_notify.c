/*
 * PKG Manager X - PS4-only notification and user-service policy
 */

#include "ps4_notify.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

ps4_user_service_state_t ps4_user_service_classify(int rc) {
    if (rc == 0) return PS4_USER_SERVICE_READY;
    if (rc == PKGMGR_USER_SERVICE_ERROR_ALREADY_INITIALIZED) return PS4_USER_SERVICE_ALREADY_INITIALIZED;
    return PS4_USER_SERVICE_FAILED;
}

int ps4_format_scan_notification(char *out, size_t out_sz, const char *version, int count) {
    if (!out || out_sz == 0) return -1;
    return snprintf(out, out_sz, "PKG Manager X v%s\nFound %d packages",
                    (version && version[0]) ? version : "?", count);
}

int ps4_is_manager_tile(const char *title_id) {
    return title_id && strcasecmp(title_id, PKGMGR_TILE_TITLE_ID) == 0;
}

int ps4_suppress_install_notification(const char *console, const char *title_id) {
    return console && strcmp(console, "ps4") == 0 && ps4_is_manager_tile(title_id);
}
