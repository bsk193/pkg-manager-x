/*
 * PKG Manager X - Package Platform Detection & Install Gating
 */

#include "pkg_platform.h"
#include "platform.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void set_platform(pkg_detail_t *d, const char *p) {
    strncpy(d->platform, p, sizeof(d->platform) - 1);
    d->platform[sizeof(d->platform) - 1] = '\0';
}

void pkg_platform_note_header(pkg_detail_t *d, const uint8_t *hdr, size_t hdr_len) {
    if (!d || !hdr || hdr_len < 4) return;
    if (memcmp(hdr, "\x7f" "FIH", 4) == 0) set_platform(d, "ps5");
}

void pkg_platform_note_param_json(pkg_detail_t *d) {
    /* param.json only exists in PS5 packages; it overrides an sfo hint. */
    if (d) set_platform(d, "ps5");
}

void pkg_platform_note_param_sfo(pkg_detail_t *d) {
    /* PS5 packages may carry a legacy param.sfo next to param.json, so an
     * sfo never overrides stronger evidence. */
    if (d && d->platform[0] == '\0') set_platform(d, "ps4");
}

const char *pkg_platform_from_title_id(const char *title_id) {
    if (!title_id) return "";
    if (strncasecmp(title_id, "PPSA", 4) == 0) return "ps5";
    if (strncasecmp(title_id, "CUSA", 4) == 0) return "ps4";
    return "";
}

/* Component matches "ps4"/"ps5" exactly or as a prefix followed by a
 * separator ("PS4 Games", "ps5_pkgs", "PS4-Backups"). */
static const char *component_platform(const char *c, size_t len) {
    if (len < 3 || strncasecmp(c, "ps", 2) != 0) return "";
    if (c[2] != '4' && c[2] != '5') return "";
    if (len > 3 && isalnum((unsigned char)c[3])) return "";
    return c[2] == '4' ? "ps4" : "ps5";
}

const char *pkg_platform_folder_name(const char *name) {
    return name ? component_platform(name, strlen(name)) : "";
}

const char *pkg_platform_folder_hint(const char *path) {
    if (!path) return "";
    /* Skip the scheme and host of URLs so a server named "ps4" does not
     * count; SMB share names and folders below them do. */
    const char *p = strstr(path, "://");
    if (p) {
        p = strchr(p + 3, '/');
        if (!p) return "";
    } else {
        p = path;
    }

    const char *hint = "";
    while (*p) {
        while (*p == '/') p++;
        const char *end = strchr(p, '/');
        if (!end) break; /* last component is the file name */
        const char *h = component_platform(p, (size_t)(end - p));
        if (h[0]) hint = h; /* deepest folder wins */
        p = end;
    }
    return hint;
}

void pkg_platform_finalize(pkg_detail_t *d) {
    if (!d || d->platform[0] != '\0') return;
    const char *p = pkg_platform_from_title_id(d->title_id);
    if (!p[0]) p = pkg_platform_folder_hint(d->path);
    if (p[0]) set_platform(d, p);
}

int pkg_platform_folder_mismatch(const pkg_detail_t *d) {
    if (!d || d->platform[0] == '\0') return 0;
    const char *hint = pkg_platform_folder_hint(d->path);
    return hint[0] != '\0' && strcmp(hint, d->platform) != 0;
}

const char *pkg_platform_console(void) {
#if PKGMGR_ON_CONSOLE
    return PKGMGR_CONSOLE_NAME;
#else
    const char *env = getenv("PKGMGR_CONSOLE");
    if (env && strcasecmp(env, "ps4") == 0) return "ps4";
    return "ps5";
#endif
}

int pkg_platform_can_install(const pkg_detail_t *d, const char **reason) {
    if (reason) *reason = "";
    if (!d) return 1;
    if (strcmp(pkg_platform_console(), "ps4") == 0 && strcmp(d->platform, "ps5") == 0) {
        if (reason) *reason = PKG_PLATFORM_REASON_PS5_ON_PS4;
        return 0;
    }
    return 1;
}
