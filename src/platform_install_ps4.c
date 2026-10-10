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
/* A download task for the same content ID already exists, typically left
 * by an earlier failed install (CE-32928-4). */
#define BGFT_ERROR_TASK_DUPLICATED                    0x80990015u
/* e.g. an update registered through RegisterTask instead of the patch call. */
#define BGFT_ERROR_INVALID_ARGUMENT                   0x80990004u

/* OrbisBgftTaskSubType */
#define BGFT_TASK_SUB_TYPE_UNKNOWN    0
#define BGFT_TASK_SUB_TYPE_GAME       6
#define BGFT_TASK_SUB_TYPE_GAME_AC    7
#define BGFT_TASK_SUB_TYPE_GAME_PATCH 8

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
typedef int (*bgft_register_fn)(bgft_download_param_t *, int *task_id);
typedef int (*bgft_start_fn)(int task_id);
typedef int (*bgft_progress_fn)(int task_id, bgft_task_progress_t *);
typedef int (*bgft_find_fn)(const char *content_id, int sub_type, int *task_id);
typedef int (*bgft_task_fn)(int task_id);

/* Local-file install (PS4-Store, SSPI): the package is already on the
 * console's drive and BGFT installs it from there. */
typedef struct {
    bgft_download_param_t param;
    unsigned int slot;
} bgft_download_param_ex_t;
typedef int (*bgft_register_ex_fn)(bgft_download_param_ex_t *, int *task_id);

static struct {
    int ready;
    int module;
    void *heap;
    bgft_init_fn init;
    bgft_term_fn term;
    bgft_register_fn register_task;
    bgft_register_fn register_patch; /* optional: updates (PS4DP) */
    bgft_start_fn start_task;
    bgft_progress_fn get_progress;
    bgft_find_fn find_task;        /* optional: duplicate-task recovery */
    bgft_task_fn stop_task;        /* optional */
    bgft_task_fn unregister_task;  /* optional */
    bgft_register_ex_fn register_storage; /* optional: local-file installs */
    int task_id;
    char content_id[64];
} g_bgft = { 0, -1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, "" };

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
    /* req->uri is an HTTP stream, not a local package path. StorageEx
     * registration treats that URI as a filename and fails with 0x8099006A.
     * Use the URL registration API, as Remote Package Installer does. */
    g_bgft.register_task = (bgft_register_fn)bgft_sym("sceBgftServiceIntDownloadRegisterTask",
                                                   "sceBgftServiceDownloadRegisterTask");
    /* Updates (PS4DP) are rejected by RegisterTask with 0x80990004
     * (INVALID_ARGUMENT); patch packages go through the debug registration
     * with the same parameters (as ezremote-client / ps4-store do). */
    g_bgft.register_patch = (bgft_register_fn)bgft_sym("sceBgftServiceIntDebugDownloadRegisterPkg", NULL);
    g_bgft.register_storage = (bgft_register_ex_fn)bgft_sym("sceBgftServiceIntDownloadRegisterTaskByStorageEx", NULL);
    g_bgft.start_task = (bgft_start_fn)bgft_sym("sceBgftServiceIntDownloadStartTask",
                                                "sceBgftServiceDownloadStartTask");
    g_bgft.get_progress = (bgft_progress_fn)bgft_sym("sceBgftServiceIntDownloadGetProgress",
                                                     "sceBgftServiceDownloadGetProgress");
    /* Removing a task left by a failed install; installs still work
     * without these (only the automatic 0x80990015 recovery is lost). */
    g_bgft.find_task = (bgft_find_fn)bgft_sym("sceBgftServiceIntDownloadFindActiveTask",
                                              "sceBgftServiceDownloadFindTaskByContentId");
    g_bgft.stop_task = (bgft_task_fn)bgft_sym("sceBgftServiceIntDownloadStopTask",
                                              "sceBgftServiceDownloadStopTask");
    g_bgft.unregister_task = (bgft_task_fn)bgft_sym("sceBgftServiceIntDownloadUnregisterTask", NULL);
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

/* Stops and unregisters a download task. 0 when it is gone. */
static int bgft_remove_task(int task_id, const char *why) {
    if (task_id < 0 || !g_bgft.unregister_task) return -1;
    int stop = g_bgft.stop_task ? g_bgft.stop_task(task_id) : 0;
    int unreg = g_bgft.unregister_task(task_id);
    install_log("[BGFT] remove task %d (%s): stop -> 0x%08X, unregister -> 0x%08X",
                task_id, why, stop, unreg);
    return unreg == 0 ? 0 : -1;
}

static int bgft_sub_type(const char *package_type) {
    if (strcmp(package_type, "PS4AC") == 0) return BGFT_TASK_SUB_TYPE_GAME_AC;
    if (strcmp(package_type, "PS4DP") == 0) return BGFT_TASK_SUB_TYPE_GAME_PATCH;
    return BGFT_TASK_SUB_TYPE_GAME;
}

/* TASK_DUPLICATED: find the leftover task for this content ID (its own
 * sub type first, then the others) and remove it. 0 when one was removed. */
static int bgft_clear_duplicate(const char *content_id, const char *package_type) {
    if (!content_id || !content_id[0] || !g_bgft.find_task || !g_bgft.unregister_task) return -1;
    const int own = bgft_sub_type(package_type);
    const int order[] = { own, BGFT_TASK_SUB_TYPE_GAME, BGFT_TASK_SUB_TYPE_GAME_AC,
                          BGFT_TASK_SUB_TYPE_GAME_PATCH, BGFT_TASK_SUB_TYPE_UNKNOWN };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (i > 0 && order[i] == own) continue;
        int task_id = -1;
        int rc = g_bgft.find_task(content_id, order[i], &task_id);
        install_log("[BGFT] find task %s sub_type=%d -> 0x%08X task=%d", content_id, order[i], rc, task_id);
        if (rc == 0 && task_id >= 0) {
            return bgft_remove_task(task_id, "leftover from an earlier install");
        }
    }
    return -1;
}

/* Before every install: remove every download task already registered for
 * this content ID (any sub type), e.g. from an earlier failed, canceled or
 * crashed attempt, so it cannot block or race the new one. Returns how
 * many were removed. */
static int bgft_remove_leftovers(const char *content_id) {
    if (!content_id || !content_id[0] || !g_bgft.find_task || !g_bgft.unregister_task) return 0;
    const int types[] = { BGFT_TASK_SUB_TYPE_GAME, BGFT_TASK_SUB_TYPE_GAME_AC,
                          BGFT_TASK_SUB_TYPE_GAME_PATCH, BGFT_TASK_SUB_TYPE_UNKNOWN };
    int removed = 0;
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        /* A few per type at most; stop when nothing (more) is found. */
        for (int n = 0; n < 4; n++) {
            int task_id = -1;
            if (g_bgft.find_task(content_id, types[i], &task_id) != 0 || task_id < 0) break;
            if (bgft_remove_task(task_id, "leftover found before install") != 0) break;
            removed++;
        }
    }
    if (removed) install_log("[BGFT] removed %d leftover task(s) for %s", removed, content_id);
    return removed;
}

int platform_install_start(const platform_install_request_t *req,
                           char *out_content_id, size_t content_id_size,
                           platform_install_canceled_fn canceled) {
    (void)canceled; /* BGFT registration returns immediately */
    if (!g_bgft.ready) return -1;
    g_bgft.task_id = -1;

    int user_id = -1;
    if (sceUserServiceGetForegroundUser(&user_id) != 0) user_id = -1;

    /* BGFT keeps pointers into these strings until the task starts. */
    static char s_uri[1024], s_name[256], s_cid[64];
    snprintf(s_uri, sizeof(s_uri), "%s", req->uri);
    snprintf(s_name, sizeof(s_name), "%s",
             (req->title_name && req->title_name[0]) ? req->title_name : req->display_name);
    snprintf(s_cid, sizeof(s_cid), "%s", req->content_id ? req->content_id : "");

    bgft_download_param_t p;
    memset(&p, 0, sizeof(p));
    p.user_id = user_id;
    p.entitlement_type = 5;
    p.id = s_cid;
    p.content_url = s_uri;
    p.content_ex_url = "";
    p.content_name = s_name;
    p.icon_path = "";
    p.sku_id = "";
    p.option = BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM;
    p.playgo_scenario_id = "0";
    p.release_date = "";
    p.package_type = bgft_package_type(req);
    p.package_sub_type = "";
    p.package_size = (unsigned long)req->package_size;

    /* Updates register through the patch call; everything else (and
     * updates on firmware without it) through RegisterTask. */
    int is_patch = strcmp(p.package_type, "PS4DP") == 0 && g_bgft.register_patch;
    bgft_register_fn reg = is_patch ? g_bgft.register_patch : g_bgft.register_task;

    bgft_remove_leftovers(s_cid);

    int task_id = -1;
    int ret = reg(&p, &task_id);
    install_log("[BGFT] register%s type=%s size=%llu user=%d -> 0x%08X task=%d",
                is_patch ? " (patch)" : "", p.package_type, (unsigned long long)req->package_size,
                user_id, ret, task_id);
    if ((uint32_t)ret == BGFT_ERROR_TASK_DUPLICATED && bgft_clear_duplicate(s_cid, p.package_type) == 0) {
        /* An earlier failed install left its task behind: retry once. */
        task_id = -1;
        ret = reg(&p, &task_id);
        install_log("[BGFT] register (after removing leftover) -> 0x%08X task=%d", ret, task_id);
    }
    if (ret != 0) return ret;

    ret = g_bgft.start_task(task_id);
    install_log("[BGFT] start task %d -> 0x%08X", task_id, ret);
    if (ret != 0) {
        /* Don't leave a registered-but-dead task to block the next try. */
        bgft_remove_task(task_id, "start failed");
        return ret;
    }

    g_bgft.task_id = task_id;
    snprintf(g_bgft.content_id, sizeof(g_bgft.content_id), "%s", s_cid);
    if (out_content_id && content_id_size > 0) snprintf(out_content_id, content_id_size, "%s", s_cid);
    return 0;
}

/* Installs a package file already on the console (local_path, e.g.
 * /user/data/pkgmgr/dl/x.pkg) through BGFT's storage route, the way
 * PS4-Store and SSPI do. Used when the URL route rejects a package
 * (0x80990004 at a section end) that installs fine from local storage.
 * Same contract as platform_install_start. */
int platform_install_start_local(const platform_install_request_t *req, const char *local_path,
                                 char *out_content_id, size_t content_id_size) {
    if (!g_bgft.ready || !local_path || !local_path[0]) return -1;
    if (!g_bgft.register_storage) {
        install_log("[BGFT] local install unavailable: sceBgftServiceIntDownloadRegisterTaskByStorageEx missing");
        return -1;
    }
    g_bgft.task_id = -1;

    int user_id = -1;
    if (sceUserServiceGetForegroundUser(&user_id) != 0) user_id = -1;

    static char s_path[512], s_name[256], s_cid[64];
    snprintf(s_path, sizeof(s_path), "%s", local_path);
    snprintf(s_name, sizeof(s_name), "%s",
             (req->title_name && req->title_name[0]) ? req->title_name : req->display_name);
    snprintf(s_cid, sizeof(s_cid), "%s", req->content_id ? req->content_id : "");

    bgft_download_param_ex_t ex;
    memset(&ex, 0, sizeof(ex));
    ex.param.user_id = user_id;
    ex.param.entitlement_type = 5;
    ex.param.id = "";
    ex.param.content_url = s_path;
    ex.param.content_ex_url = "";
    ex.param.content_name = s_name;
    ex.param.icon_path = "";
    ex.param.sku_id = "";
    ex.param.option = BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM;
    ex.param.playgo_scenario_id = "0";
    ex.param.release_date = "";
    /* Storage route: the package header supplies type and size. */
    ex.param.package_type = "";
    ex.param.package_sub_type = "";
    ex.param.package_size = (unsigned long)req->package_size;
    ex.slot = 0;

    bgft_remove_leftovers(s_cid);

    int task_id = -1;
    int ret = g_bgft.register_storage(&ex, &task_id);
    install_log("[BGFT] register (local) path=%s size=%llu user=%d -> 0x%08X task=%d",
                s_path, (unsigned long long)req->package_size, user_id, ret, task_id);
    if ((uint32_t)ret == BGFT_ERROR_TASK_DUPLICATED && bgft_clear_duplicate(s_cid, "PS4GD") == 0) {
        task_id = -1;
        ret = g_bgft.register_storage(&ex, &task_id);
        install_log("[BGFT] register (local, after removing leftover) -> 0x%08X task=%d", ret, task_id);
    }
    if (ret != 0) return ret;

    ret = g_bgft.start_task(task_id);
    install_log("[BGFT] start task %d -> 0x%08X", task_id, ret);
    if (ret != 0) {
        bgft_remove_task(task_id, "start failed");
        return ret;
    }
    g_bgft.task_id = task_id;
    snprintf(g_bgft.content_id, sizeof(g_bgft.content_id), "%s", s_cid);
    if (out_content_id && content_id_size > 0) snprintf(out_content_id, content_id_size, "%s", s_cid);
    return 0;
}

int platform_install_poll(const char *content_id, platform_install_progress_t *out) {
    (void)content_id;
    if (!out || !g_bgft.ready || !g_bgft.get_progress || g_bgft.task_id < 0) return PLATFORM_INSTALL_NO_STATUS;
    bgft_task_progress_t pr;
    memset(&pr, 0, sizeof(pr));
    if (g_bgft.get_progress(g_bgft.task_id, &pr) != 0) return PLATFORM_INSTALL_NO_STATUS;
    memset(out, 0, sizeof(*out));
    out->downloaded_size = pr.transferred_total;
    out->total_size = pr.length_total;
    if (pr.error_result != 0) {
        snprintf(out->status, sizeof(out->status), "error");
        out->error_code = pr.error_result;
    } else {
        /* Completion is confirmed by installer.c's app.db / version checks. */
        snprintf(out->status, sizeof(out->status), "downloading");
    }
    return 0;
}

void platform_install_close(void) {
    /* The BGFT task keeps running in the system; only forget our handle. */
    g_bgft.task_id = -1;
}

void platform_install_discard(void) {
    /* Failed or canceled: remove our task so it cannot block the next
     * attempt with 0x80990015. */
    if (g_bgft.ready && g_bgft.task_id >= 0) {
        bgft_remove_task(g_bgft.task_id, "install failed or canceled");
    }
    g_bgft.task_id = -1;
}

const char *platform_install_strerror(int code) {
    if (code == 0) return "OK";
    switch ((uint32_t)code) {
    case BGFT_ERROR_SAME_APPLICATION_ALREADY_INSTALLED: return "BGFT_ERROR_SAME_APPLICATION_ALREADY_INSTALLED";
    case BGFT_ERROR_TASK_DUPLICATED: return "BGFT_ERROR_TASK_DUPLICATED";
    case BGFT_ERROR_INVALID_ARGUMENT: return "BGFT_ERROR_INVALID_ARGUMENT";
    default: return NULL;
    }
}

int platform_install_is_transient(int code) {
    (void)code;
    return 0;
}

#endif /* PKGMGR_CONSOLE_PS4 */
