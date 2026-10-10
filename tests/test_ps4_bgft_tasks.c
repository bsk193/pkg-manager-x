/* Real PS4 BGFT backend with mocked console exports: leftover-task handling.
 *
 * A failed install used to leave its BGFT task registered, so the next
 * install of the same content failed with 0x80990015 (TASK_DUPLICATED).
 */
#define PS4_BUILD 1
#include <assert.h>
#include <stdarg.h>
#include "../src/platform_install_ps4.c"

#define DUP ((int)0x80990015)

static int reg_results[4], reg_calls, next_task = 100;
static int start_result, start_calls;
static int found_task = -1, found_sub_type = -1, find_calls;
static int stopped[8], n_stopped, unregistered[8], n_unregistered;
static int have_cleanup_syms = 1;
static int have_patch_sym = 1, patch_calls;

static int mock_init(bgft_init_params_t *p) { (void)p; return 0; }
static int mock_term(void) { return 0; }
static int mock_register(bgft_download_param_t *p, int *task) {
    (void)p;
    int rc = reg_results[reg_calls++];
    *task = rc == 0 ? next_task++ : -1;
    return rc;
}
static int mock_register_patch(bgft_download_param_t *p, int *task) {
    assert(strcmp(p->package_type, "PS4DP") == 0);
    patch_calls++;
    return mock_register(p, task);
}
static int mock_start(int task) { (void)task; start_calls++; return start_result; }
static int mock_progress(int task, bgft_task_progress_t *pr) { (void)task; memset(pr, 0, sizeof(*pr)); return 0; }
static int mock_find(const char *cid, int sub_type, int *task) {
    find_calls++;
    assert(cid && cid[0]);
    if (found_task >= 0 && sub_type == found_sub_type) { *task = found_task; return 0; }
    *task = -1;
    return (int)0x80990001;
}
static int mock_stop(int task) { stopped[n_stopped++] = task; return 0; }
static int mock_unregister(int task) { unregistered[n_unregistered++] = task; return 0; }

int sceKernelLoadStartModule(const char *p, size_t a, const void *v, unsigned int f, void *o, int *r) {
    (void)p; (void)a; (void)v; (void)f; (void)o; (void)r;
    return 1;
}
int sceKernelDlsym(int handle, const char *name, void **out) {
    (void)handle;
    *out = NULL;
    if (!strcmp(name, "sceBgftServiceIntInit")) *out = (void *)mock_init;
    if (!strcmp(name, "sceBgftServiceIntTerm")) *out = (void *)mock_term;
    if (!strcmp(name, "sceBgftServiceIntDownloadRegisterTask")) *out = (void *)mock_register;
    if (!strcmp(name, "sceBgftServiceIntDownloadStartTask")) *out = (void *)mock_start;
    if (!strcmp(name, "sceBgftServiceIntDownloadGetProgress")) *out = (void *)mock_progress;
    if (have_patch_sym && !strcmp(name, "sceBgftServiceIntDebugDownloadRegisterPkg")) *out = (void *)mock_register_patch;
    if (have_cleanup_syms) {
        if (!strcmp(name, "sceBgftServiceIntDownloadFindActiveTask")) *out = (void *)mock_find;
        if (!strcmp(name, "sceBgftServiceIntDownloadStopTask")) *out = (void *)mock_stop;
        if (!strcmp(name, "sceBgftServiceIntDownloadUnregisterTask")) *out = (void *)mock_unregister;
    }
    return *out ? 0 : -1;
}
int sceUserServiceGetForegroundUser(int *user) { *user = 1; return 0; }
int sceAppInstUtilInitialize(void) { return 0; }
int sceAppInstUtilTerminate(void) { return 0; }
void install_log(const char *fmt, ...) { (void)fmt; }
void ps5_notify(const char *fmt, ...) { (void)fmt; }

static void reset(int r0, int r1, int start_rc, int found, int found_type) {
    memset(reg_results, 0, sizeof(reg_results));
    reg_results[0] = r0;
    reg_results[1] = r1;
    reg_calls = start_calls = find_calls = n_stopped = n_unregistered = patch_calls = 0;
    next_task = 100;
    start_result = start_rc;
    found_task = found;
    found_sub_type = found_type;
    assert(platform_install_init() == 0);
}

static int start_install(const char *kind, const char *category) {
    platform_install_request_t req = {
        .uri = "http://127.0.0.1:18841/stream/install/package-1-1.pkg",
        .display_name = "Leon Costume", .title_name = "Leon Costume",
        .content_id = "EP0102-CUSA09171_00-BH20000COSDLC002",
        .pkg_kind = kind, .category = category, .package_size = 53608448
    };
    char cid[64] = "";
    return platform_install_start(&req, cid, sizeof(cid), NULL);
}

int main(void) {
    /* 1. Leftover DLC task from a failed install: found by content ID (DLC
     *    sub type first), stopped + unregistered, registration retried. */
    reset(DUP, 0, 0, 34, BGFT_TASK_SUB_TYPE_GAME_AC);
    assert(start_install("dlc", "ac") == 0);
    assert(reg_calls == 2 && start_calls == 1 && find_calls == 1);
    assert(n_stopped == 1 && stopped[0] == 34);
    assert(n_unregistered == 1 && unregistered[0] == 34);
    platform_install_close(); /* success path: our task stays */
    assert(n_unregistered == 1);
    platform_install_shutdown();

    /* 2. Leftover registered under another sub type: still found. */
    reset(DUP, 0, 0, 51, BGFT_TASK_SUB_TYPE_GAME);
    assert(start_install("dlc", "ac") == 0);
    assert(n_unregistered == 1 && unregistered[0] == 51 && reg_calls == 2);
    platform_install_shutdown();

    /* 3. Nothing to remove: the original error comes back, named. */
    reset(DUP, 0, 0, -1, -1);
    assert(start_install("base", "gd") == DUP);
    /* GAME, GAME_AC, GAME_PATCH, UNKNOWN: each sub type looked up once. */
    assert(reg_calls == 1 && n_unregistered == 0 && find_calls == 4);
    assert(strcmp(platform_install_strerror(DUP), "BGFT_ERROR_TASK_DUPLICATED") == 0);
    platform_install_shutdown();

    /* 4. Registered but start failed: the new task is removed at once. */
    reset(0, 0, (int)0x80990003, -1, -1);
    assert(start_install("base", "gd") == (int)0x80990003);
    assert(n_unregistered == 1 && unregistered[0] == 100);
    platform_install_shutdown();

    /* 5. Failed / canceled install: discard removes our task, once. */
    reset(0, 0, 0, -1, -1);
    assert(start_install("update", "gp") == 0);
    platform_install_discard();
    assert(n_stopped == 1 && stopped[0] == 100);
    assert(n_unregistered == 1 && unregistered[0] == 100);
    platform_install_discard();
    assert(n_unregistered == 1);
    platform_install_shutdown();

    /* 6. Firmware without the cleanup exports: installs still work and a
     *    duplicate is reported instead of crashing. */
    have_cleanup_syms = 0;
    reset(0, 0, 0, -1, -1);
    assert(start_install("base", "gd") == 0);
    platform_install_discard();
    assert(n_unregistered == 0);
    platform_install_shutdown();
    reset(DUP, 0, 0, 34, BGFT_TASK_SUB_TYPE_GAME);
    assert(start_install("base", "gd") == DUP && find_calls == 0);
    platform_install_shutdown();

    /* 7. Updates register through the patch call (RegisterTask rejects them
     *    with 0x80990004 INVALID_ARGUMENT); games and DLC keep RegisterTask. */
    have_cleanup_syms = 1;
    reset(0, 0, 0, -1, -1);
    assert(start_install("update", "gp") == 0 && patch_calls == 1 && reg_calls == 1);
    platform_install_shutdown();
    reset(0, 0, 0, -1, -1);
    assert(start_install("base", "gd") == 0 && patch_calls == 0 && reg_calls == 1);
    platform_install_shutdown();
    reset(0, 0, 0, -1, -1);
    assert(start_install("dlc", "ac") == 0 && patch_calls == 0);
    platform_install_shutdown();
    /* Leftover update task: removed, and the retry uses the patch call too. */
    reset(DUP, 0, 0, 34, BGFT_TASK_SUB_TYPE_GAME_PATCH);
    assert(start_install("update", "gp") == 0 && patch_calls == 2);
    assert(n_unregistered == 1 && unregistered[0] == 34);
    platform_install_shutdown();
    /* Firmware without the patch export: updates fall back to RegisterTask. */
    have_patch_sym = 0;
    reset(0, 0, 0, -1, -1);
    assert(start_install("update", "gp") == 0 && patch_calls == 0 && reg_calls == 1);
    platform_install_shutdown();
    assert(strcmp(platform_install_strerror((int)0x80990004), "BGFT_ERROR_INVALID_ARGUMENT") == 0);

    puts("PS4 BGFT leftover tasks: passed");
    return 0;
}
