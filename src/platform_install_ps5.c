/*
 * PKG Manager X - PS5 install backend
 *
 * Thin adapter over upstream's install service (install_service.c): every
 * install runs sceAppInstUtilInstallByPackage in a fresh helper process,
 * which fixes follow-up installs on firmware 9.60+ (upstream v1.3.0).
 */

#include "platform.h"

#if PKGMGR_CONSOLE_PS5

#include "platform_install.h"
#include "install_service.h"
#include "notification.h"

#include <stdio.h>
#include <string.h>

extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilTerminate(void);

static install_service_t g_service = INSTALL_SERVICE_INIT;

int platform_install_init(void) {
    /* Catalog DLC queries and leftover removal still use the parent's
     * AppInstUtil client. Package submission/status and shortcut registration
     * use separate processes and never share this session. */
    int ret = sceAppInstUtilInitialize();
    if (ret != 0) {
        printf("[PKG Manager] sceAppInstUtilInitialize returned 0x%08X\n", ret);
        ps5_notify("PKG Manager: app install service returned 0x%08X", ret);
    }
    return ret;
}

void platform_install_shutdown(void) {
    platform_install_close();
    sceAppInstUtilTerminate();
}

int platform_install_start(const platform_install_request_t *req,
                           char *out_content_id, size_t content_id_size,
                           platform_install_canceled_fn canceled) {
    platform_install_close();
    pkg_info_t info;
    memset(&info, 0, sizeof(info));
    int ret = install_service_start(&g_service, req->uri, req->display_name, &info, canceled);
    if (out_content_id && content_id_size > 0) {
        snprintf(out_content_id, content_id_size, "%.*s", (int)sizeof(info.content_id), info.content_id);
    }
    return ret;
}

int platform_install_poll(const char *content_id, platform_install_progress_t *out) {
    (void)content_id; /* the helper tracks its own install */
    if (!out) return PLATFORM_INSTALL_NO_STATUS;
    memset(out, 0, sizeof(*out));
    SceAppInstallStatusInstalled st;
    memset(&st, 0, sizeof(st));
    int ret = install_service_status(&g_service, &st);
    if (ret == INSTALL_SERVICE_CANCELED) return PLATFORM_INSTALL_CANCELED;
    if (ret == INSTALL_SERVICE_DISCONNECTED || ret == INSTALL_SERVICE_TIMEOUT) {
        out->error_code = ret;
        return PLATFORM_INSTALL_LOST;
    }
    if (ret != 0) return PLATFORM_INSTALL_NO_STATUS;
    snprintf(out->status, sizeof(out->status), "%.*s", (int)sizeof(st.status), st.status);
    out->error_code = st.error_info.error_code;
    out->downloaded_size = st.downloaded_size;
    return 0;
}

void platform_install_close(void) {
    install_service_close(&g_service);
}

/* Human-readable names for installer/playgo error codes (verified against
   etaHEN error_translator and on-console results). Unknown codes -> NULL. */
const char *platform_install_strerror(int code) {
    if (code == 0) {
        return "OK";
    }
    switch (code) {
    case INSTALL_SERVICE_UNAVAILABLE: return "INSTALL_HELPER_UNAVAILABLE";
    case INSTALL_SERVICE_DISCONNECTED: return "INSTALL_HELPER_DISCONNECTED";
    case INSTALL_SERVICE_CANCELED: return "INSTALL_HELPER_CANCELED";
    case INSTALL_SERVICE_TIMEOUT: return "INSTALL_HELPER_TIMEOUT";
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
   still finalizes the base): safe to retry with a fresh helper process.
   Anything else, including PARAM, fails immediately. */
int platform_install_is_transient(int code) {
    uint32_t c = (uint32_t)code;
    return c == 0x80B2116Fu || c == 0x80B2100Du || c == 0x80B2100Eu;
}

#endif /* PKGMGR_CONSOLE_PS5 */
