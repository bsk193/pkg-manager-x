/*
 * PKG Manager X - PS5 install backend (sceAppInstUtil)
 *
 * Moved verbatim from installer.c so the worker can drive either console.
 */

#include "platform.h"

#if PKGMGR_CONSOLE_PS5

#include "platform_install.h"
#include "notification.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct pkg_metadata {
    const char *uri;
    const char *ex_uri;
    const char *playgo_scenario_id;
    const char *content_id;
    const char *content_name;
    const char *icon_url;
} pkg_metadata_t;

typedef struct pkg_info {
    char content_id[48];
    int type;
    int platform;
} pkg_info_t;

typedef struct playgo_info {
    char languages[30][8];
    char playgo_scenario_ids[64][3];
    char content_ids[64][48];
    unsigned char unknown[6480];
} playgo_info_t;

typedef struct {
    int32_t error_code;
    int32_t version;
    char description[512];
    char type[9];
} SceAppInstallErrorInfo;

typedef struct {
    char status[16];
    char src_type[8];
    uint32_t remain_time;
    uint64_t downloaded_size;
    uint64_t initial_chunk_size;
    uint64_t total_size;
    uint32_t promote_progress;
    SceAppInstallErrorInfo error_info;
    int32_t local_copy_percent;
    bool is_copy_only;
} SceAppInstallStatusInstalled;

extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilTerminate(void);
extern int sceAppInstUtilInstallByPackage(const pkg_metadata_t *meta, pkg_info_t *info, playgo_info_t *playgo);
extern int sceAppInstUtilGetInstallStatus(const char *content_id, SceAppInstallStatusInstalled *status);

int platform_install_init(void) {
    int ret = sceAppInstUtilInitialize();
    if (ret != 0) {
        printf("[PKG Manager] sceAppInstUtilInitialize returned 0x%08X\n", ret);
        ps5_notify("PKG Manager: app install service returned 0x%08X", ret);
    }
    return ret;
}

void platform_install_shutdown(void) {
    sceAppInstUtilTerminate();
}

int platform_install_start(const platform_install_request_t *req,
                           char *out_content_id, size_t content_id_size) {
    /* ShellCore may read meta strings after return: keep them static. */
    static pkg_metadata_t meta;
    static pkg_info_t info;
    static playgo_info_t playgo;
    memset(&meta, 0, sizeof(meta));
    memset(&info, 0, sizeof(info));
    memset(&playgo, 0, sizeof(playgo));
    meta.uri = req->uri;
    meta.ex_uri = "";
    meta.playgo_scenario_id = "";
    meta.content_id = "";
    meta.content_name = req->display_name;
    meta.icon_url = "";

    int ret = sceAppInstUtilInstallByPackage(&meta, &info, &playgo);
    if (out_content_id && content_id_size > 0) {
        snprintf(out_content_id, content_id_size, "%.*s", (int)sizeof(info.content_id), info.content_id);
    }
    return ret;
}

int platform_install_poll(const char *content_id, platform_install_progress_t *out) {
    if (!content_id || content_id[0] == '\0' || !out) return -1;
    SceAppInstallStatusInstalled st;
    memset(&st, 0, sizeof(st));
    if (sceAppInstUtilGetInstallStatus(content_id, &st) != 0) return -1;
    memset(out, 0, sizeof(*out));
    snprintf(out->status, sizeof(out->status), "%.*s", (int)sizeof(st.status), st.status);
    out->error_code = st.error_info.error_code;
    out->downloaded_size = st.downloaded_size;
    return 0;
}

/* Human-readable names for installer/playgo error codes (verified against
   etaHEN error_translator and on-console results). Unknown codes -> NULL. */
const char *platform_install_strerror(int code) {
    if (code == 0) {
        return "OK";
    }
    switch ((uint32_t)code) {
    case 0x80A30001u: return "APP_INSTALLER_ERROR_UNKNOWN";
    case 0x80A30002u: return "APP_INSTALLER_ERROR_NOSPACE";
    case 0x80A30003u: return "APP_INSTALLER_ERROR_PARAM";
    case 0x80B21164u: return "PLAYGO_ERROR_CORE_INVALID_CONTENT_ID";
    case 0x80B21167u: return "PLAYGO_ERROR_CORE_CONTENT_ID_MISMATCH";
    case 0x80B2116Au: return "PLAYGO_ERROR_CORE_REQUIRE_FULLY_INSTALLED_APPLICATION";
    case 0x80B2116Eu: return "PLAYGO_ERROR_CORE_INVALID_VERSION";
    case 0x80B21170u: return "PLAYGO_ERROR_CORE_PATCH_INVALID_RANGE";
    case 0x80B2116Fu: return "PLAYGO_ERROR_CORE_INVALID_SLOT";
    case 0x80B2100Du: return "PLAYGO_ERROR_CORE_NOT_READY";
    case 0x80B2100Eu: return "PLAYGO_ERROR_CORE_TIMEOUT";
    default: return NULL;
    }
}

/* Slot-family errors are transient (e.g. patch installed while the system
   still finalizes the base): safe to retry with a fresh session. Anything
   else, including PARAM, fails immediately. */
int platform_install_is_transient(int code) {
    uint32_t c = (uint32_t)code;
    return c == 0x80B2116Fu || c == 0x80B2100Du || c == 0x80B2100Eu;
}

#endif /* PKGMGR_CONSOLE_PS5 */
