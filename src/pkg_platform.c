/*
 * PKG Manager X - Package Platform / Content Type Detection & Install Gating
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

/* Retail title ID prefixes (all regions). Anything else with the usual
 * AAAA00000 shape is a homebrew / self-made ID. */
static const char *const k_retail_ps4[] = { "CUSA", "PCAS", "PCJS", "PCKS", "PLAS", "PLJM", "PLJS", "PLKS", "NPXS" };
static const char *const k_retail_ps5[] = { "PPSA", "ECAS", "ECJS", "ECKS", "ELAS", "ELJM", "ELJS", "ELKS" };

static int has_prefix_in(const char *tid, const char *const *list, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (strncasecmp(tid, list[i], 4) == 0) return 1;
    }
    return 0;
}

const char *pkg_platform_from_title_id(const char *title_id) {
    if (!title_id) return "";
    if (has_prefix_in(title_id, k_retail_ps5, sizeof(k_retail_ps5) / sizeof(k_retail_ps5[0]))) return "ps5";
    if (has_prefix_in(title_id, k_retail_ps4, sizeof(k_retail_ps4) / sizeof(k_retail_ps4[0]))) return "ps4";
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

void pkg_platform_finalize(pkg_detail_t *d) {
    if (!d || d->platform[0] != '\0') return;
    const char *p = pkg_platform_from_title_id(d->title_id);
    if (p[0]) set_platform(d, p);
}

/* "ABCD12345" */
static int is_title_id_shape(const char *tid) {
    for (int i = 0; i < 4; i++) {
        if (!isalpha((unsigned char)tid[i])) return 0;
    }
    for (int i = 4; i < 9; i++) {
        if (!isdigit((unsigned char)tid[i])) return 0;
    }
    return 1;
}

int pkg_platform_is_homebrew(const pkg_detail_t *d) {
    if (!d) return 0;
    /* Patches and add-ons belong to a title; only applications count. */
    if (d->pkg_type == PKG_TYPE_UPDATE || d->pkg_type == PKG_TYPE_DLC) return 0;
    /* Fake-signed homebrew uses the "IV0000" publisher in its content ID
     * (IV0000-LAPY20001_00-...), including homebrew with retail-like IDs. */
    if (strncasecmp(d->content_id, "IV0000-", 7) == 0) return 1;
    if (!is_title_id_shape(d->title_id)) return 0;
    return !has_prefix_in(d->title_id, k_retail_ps4, sizeof(k_retail_ps4) / sizeof(k_retail_ps4[0])) &&
           !has_prefix_in(d->title_id, k_retail_ps5, sizeof(k_retail_ps5) / sizeof(k_retail_ps5[0]));
}

const char *pkg_platform_content_type(const pkg_detail_t *d) {
    if (!d) return "game";
    if (d->pkg_type == PKG_TYPE_UPDATE) return "update";
    if (d->pkg_type == PKG_TYPE_DLC) return "dlc";
    return pkg_platform_is_homebrew(d) ? "homebrew" : "game";
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

static volatile int g_allow_ps4_on_ps5 = 1;

void pkg_platform_set_allow_ps4_on_ps5(int allow) {
    g_allow_ps4_on_ps5 = allow ? 1 : 0;
}

int pkg_platform_get_allow_ps4_on_ps5(void) {
    return g_allow_ps4_on_ps5;
}

int pkg_platform_can_install(const pkg_detail_t *d, const char **reason) {
    if (reason) *reason = "";
    if (!d) return 1;
    const char *console = pkg_platform_console();
    if (strcmp(console, "ps4") == 0) {
        if (strcmp(d->platform, "ps5") == 0) {
            if (reason) *reason = PKG_PLATFORM_REASON_PS5_ONLY;
            return 0;
        }
        return 1;
    }
    if (strcmp(d->platform, "ps4") == 0) {
        if (pkg_platform_is_homebrew(d)) {
            if (reason) *reason = PKG_PLATFORM_REASON_PS4_HOMEBREW;
            return 0;
        }
        if (!g_allow_ps4_on_ps5) {
            if (reason) *reason = PKG_PLATFORM_REASON_PS4_DISABLED;
            return 0;
        }
    }
    return 1;
}
