/*
 * PKG Manager X - PS4 install backend (BGFT)
 *
 * Registers a background-download task pointing at the local stream URL,
 * the same mechanism flatz' Remote Package Installer uses on PS4. BGFT is
 * resolved at runtime from libSceBgft.sprx because the payload SDK ships
 * no stubs for it.
 *
 * STATUS: UNVERIFIED ON HARDWARE. The structure layouts and option values
 * below follow the public PS4 homebrew headers (OpenOrbis libSceBgft.h /
 * flatz RPI). docs/PS4.md lists the on-console checks to run first.
 */

#include "platform.h"

#if PKGMGR_CONSOLE_PS4

#include "platform_install.h"
#include "installer.h"
#include "notification.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ── libkernel / libSceUserService (stubbed by the SDK) ──────────────── */
extern int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv,
                                    unsigned int flags, void *opt, int *res);
extern int sceKernelDlsym(int handle, const char *symbol, void **addr);
extern int sceUserServiceGetForegroundUser(int *user_id);
extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilTerminate(void);

/* ── BGFT ABI ─────────────────────────────────────────────────────────── */
#define BGFT_HEAP_SIZE (1 * 1024 * 1024)

#define BGFT_TASK_OPTION_NONE                    0x0
#define BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM 0x10000

#define BGFT_ERROR_SAME_APPLICATION_ALREADY_INSTALLED 0x80990088u

typedef struct {
    void *heap;
    size_t heap_size;
} bgft_init_params_t;

typedef struct {
    int user_id;
    int entitlement_type;
    const char *id;
    const char *content_url;
    const char *content_ex_url;
    const char *content_name;
    const char *icon_path;
    const char *sku_id;
    int option;
    const char *playgo_scenario_id;
    const char *release_date;
    const char *package_type;
    const char *package_sub_type;
    unsigned long package_size;
} bgft_download_param_t;

typedef struct {
    bgft_download_param_t param;
    unsigned int slot;
} bgft_download_param_ex_t;

typedef struct {
    unsigned int bits;
    int error_result;
    unsigned long length;
    unsigned long transferred;
    unsigned long length_total;
    unsigned long transferred_total;
    unsigned int num_index;
    unsigned int num_total;
    unsigned int rest_sec;
    unsigned int rest_sec_total;
    int preparing_percent;
    int local_copy_percent;
} bgft_task_progress_t;

typedef int (*bgft_init_fn)(bgft_init_params_t *);
typedef int (*bgft_term_fn)(void);
typedef int (*bgft_register_fn)(bgft_download_param_ex_t *, int *task_id);
typedef int (*bgft_start_fn)(int task_id);
typedef int (*bgft_progress_fn)(int task_id, bgft_task_progress_t *);

static struct {
    int ready;
    int module;
    void *heap;
    bgft_init_fn init;
    bgft_term_fn term;
    bgft_register_fn register_task;
    bgft_start_fn start_task;
    bgft_progress_fn get_progress;
    int task_id;
    char content_id[64];
} g_bgft = { 0, -1, NULL, NULL, NULL, NULL, NULL, NULL, -1, "" };

static void *bgft_sym(const char *primary, const char *fallback) {
    void *addr = NULL;
    if (sceKernelDlsym(g_bgft.module, primary, &addr) == 0 && addr) return addr;
    if (fallback && sceKernelDlsym(g_bgft.module, fallback, &addr) == 0 && addr) return addr;
    install_log("[BGFT] symbol %s not found", primary);
    return NULL;
}

int platform_install_init(void) {
    /* AppInstUtil backs leftovers cleanup (AppUnInstallPat / Addcont). */
    int ai = sceAppInstUtilInitialize();
    if (ai != 0) printf("[PKG Manager] sceAppInstUtilInitialize returned 0x%08X\n", ai);

    g_bgft.module = sceKernelLoadStartModule("/system/common/lib/libSceBgft.sprx", 0, NULL, 0, NULL, NULL);
    if (g_bgft.module < 0) {
        install_log("[BGFT] loading libSceBgft.sprx failed: 0x%08X", g_bgft.module);
        ps5_notify("PKG Manager: BGFT module failed to load (0x%08X)", g_bgft.module);
        return g_bgft.module;
    }
    g_bgft.init = (bgft_init_fn)bgft_sym("sceBgftServiceIntInit", "sceBgftServiceInit");
    g_bgft.term = (bgft_term_fn)bgft_sym("sceBgftServiceIntTerm", "sceBgftServiceTerm");
    g_bgft.register_task = (bgft_register_fn)bgft_sym("sceBgftServiceIntDownloadRegisterTaskByStorageEx", NULL);
    g_bgft.start_task = (bgft_start_fn)bgft_sym("sceBgftServiceIntDownloadStartTask",
                                                "sceBgftServiceDownloadStartTask");
    g_bgft.get_progress = (bgft_progress_fn)bgft_sym("sceBgftServiceIntDownloadGetProgress",
                                                     "sceBgftServiceDownloadGetProgress");
    if (!g_bgft.init || !g_bgft.register_task || !g_bgft.start_task) {
        ps5_notify("PKG Manager: BGFT symbols missing, installs disabled");
        return -1;
    }

    g_bgft.heap = malloc(BGFT_HEAP_SIZE);
    if (!g_bgft.heap) return -1;
    memset(g_bgft.heap, 0, BGFT_HEAP_SIZE);
    bgft_init_params_t ip = { g_bgft.heap, BGFT_HEAP_SIZE };
    int ret = g_bgft.init(&ip);
    if (ret != 0) {
        install_log("[BGFT] init returned 0x%08X", ret);
        ps5_notify("PKG Manager: BGFT init returned 0x%08X", ret);
        free(g_bgft.heap);
        g_bgft.heap = NULL;
        return ret;
    }
    g_bgft.ready = 1;
    install_log("[BGFT] ready");
    return 0;
}

void platform_install_shutdown(void) {
    if (g_bgft.ready && g_bgft.term) g_bgft.term();
    g_bgft.ready = 0;
    free(g_bgft.heap);
    g_bgft.heap = NULL;
    sceAppInstUtilTerminate();
}

static const char *bgft_package_type(const platform_install_request_t *req) {
    if (req->pkg_kind && strcasecmp(req->pkg_kind, "update") == 0) return "PS4DP";
    if (req->category && strncmp(req->category, "al", 2) == 0) return "PS4AL";
    if (req->pkg_kind && strcasecmp(req->pkg_kind, "dlc") == 0) return "PS4AC";
    return "PS4GD";
}

int platform_install_start(const platform_install_request_t *req,
                           char *out_content_id, size_t content_id_size) {
    if (!g_bgft.ready) return -1;

    int user_id = -1;
    if (sceUserServiceGetForegroundUser(&user_id) != 0) user_id = -1;

    /* BGFT keeps pointers into these strings until the task starts. */
    static char s_uri[1024], s_name[256], s_cid[64];
    snprintf(s_uri, sizeof(s_uri), "%s", req->uri);
    snprintf(s_name, sizeof(s_name), "%s",
             (req->title_name && req->title_name[0]) ? req->title_name : req->display_name);
    snprintf(s_cid, sizeof(s_cid), "%s", req->content_id ? req->content_id : "");

    bgft_download_param_ex_t p;
    memset(&p, 0, sizeof(p));
    p.param.user_id = user_id;
    p.param.entitlement_type = 5;
    p.param.id = s_cid;
    p.param.content_url = s_uri;
    p.param.content_ex_url = "";
    p.param.content_name = s_name;
    p.param.icon_path = "";
    p.param.sku_id = "";
    p.param.option = BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM;
    p.param.playgo_scenario_id = "0";
    p.param.release_date = "";
    p.param.package_type = bgft_package_type(req);
    p.param.package_sub_type = "";
    p.param.package_size = (unsigned long)req->package_size;
    p.slot = 0;

    int task_id = -1;
    int ret = g_bgft.register_task(&p, &task_id);
    install_log("[BGFT] register type=%s size=%llu user=%d -> 0x%08X task=%d",
                p.param.package_type, (unsigned long long)req->package_size, user_id, ret, task_id);
    if (ret != 0) return ret;

    ret = g_bgft.start_task(task_id);
    install_log("[BGFT] start task %d -> 0x%08X", task_id, ret);
    if (ret != 0) return ret;

    g_bgft.task_id = task_id;
    snprintf(g_bgft.content_id, sizeof(g_bgft.content_id), "%s", s_cid);
    if (out_content_id && content_id_size > 0) snprintf(out_content_id, content_id_size, "%s", s_cid);
    return 0;
}

int platform_install_poll(const char *content_id, platform_install_progress_t *out) {
    (void)content_id;
    if (!out || !g_bgft.ready || !g_bgft.get_progress || g_bgft.task_id < 0) return -1;
    bgft_task_progress_t pr;
    memset(&pr, 0, sizeof(pr));
    if (g_bgft.get_progress(g_bgft.task_id, &pr) != 0) return -1;
    memset(out, 0, sizeof(*out));
    out->downloaded_size = pr.transferred_total;
    if (pr.error_result != 0) {
        snprintf(out->status, sizeof(out->status), "error");
        out->error_code = pr.error_result;
    } else {
        /* Completion is confirmed by installer.c's app.db / version checks. */
        snprintf(out->status, sizeof(out->status), "downloading");
    }
    return 0;
}

const char *platform_install_strerror(int code) {
    if (code == 0) return "OK";
    switch ((uint32_t)code) {
    case BGFT_ERROR_SAME_APPLICATION_ALREADY_INSTALLED: return "BGFT_ERROR_SAME_APPLICATION_ALREADY_INSTALLED";
    default: return NULL;
    }
}

int platform_install_is_transient(int code) {
    (void)code;
    return 0;
}

#endif /* PKGMGR_CONSOLE_PS4 */
