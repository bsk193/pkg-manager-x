/* Compile the real backend with mocked console exports. */
#define PS4_BUILD 1
#include <assert.h>
#include <stdarg.h>
#include "../src/platform_install_ps4.c"

static int registrations, storage_registrations, starts, use_fallback, have_storage;
static int mock_init(bgft_init_params_t *p) { assert(p->heap && p->heap_size); return 0; }
static int mock_term(void) { return 0; }
static int mock_register(bgft_download_param_t *p, int *task) {
    assert(p->user_id == 7);
    assert(strcmp(p->content_url, "http://127.0.0.1:18841/tile.pkg") == 0);
    assert(strcmp(p->package_type, "PS4GD") == 0);
    assert(p->package_size == 6619136);
    assert(p->entitlement_type == 5);
    registrations++; *task = 42; return 0;
}
static int mock_start(int task) { assert(task == 42); starts++; return 0; }
static int mock_register_storage(bgft_download_param_ex_t *p, int *task) {
    assert(p->param.user_id == 7);
    assert(strcmp(p->param.content_url, "/user/data/pkgmgr/dl/PKGX00001-base.pkg") == 0);
    assert(strcmp(p->param.content_name, "PKG Manager X") == 0);
    assert(p->param.package_size == 6619136);
    assert(p->param.entitlement_type == 5);
    assert(p->param.option == BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM);
    assert(p->slot == 0);
    storage_registrations++; *task = 42; return 0;
}
int sceKernelLoadStartModule(const char *p, size_t a, const void *v, unsigned int f, void *o, int *r) { return 1; }
int sceKernelDlsym(int handle, const char *name, void **out) {
    *out = NULL;
    if (!strcmp(name,"sceBgftServiceIntInit")) *out = (void *)mock_init;
    if (!strcmp(name,"sceBgftServiceIntTerm")) *out = (void *)mock_term;
    if (!strcmp(name,"sceBgftServiceIntDownloadRegisterTask") && !use_fallback) *out = (void *)mock_register;
    if (!strcmp(name,"sceBgftServiceDownloadRegisterTask") && use_fallback) *out = (void *)mock_register;
    if (!strcmp(name,"sceBgftServiceIntDownloadRegisterTaskByStorageEx") && have_storage)
        *out = (void *)mock_register_storage;
    if (!strcmp(name,"sceBgftServiceIntDownloadStartTask")) *out = (void *)mock_start;
    return *out ? 0 : -1;
}
int sceUserServiceGetForegroundUser(int *user) { *user = 7; return 0; }
int sceAppInstUtilInitialize(void) { return 0; }
int sceAppInstUtilTerminate(void) { return 0; }
void install_log(const char *fmt, ...) {}
void ps5_notify(const char *fmt, ...) {}
int main(void) {
    for (have_storage = 0; have_storage < 2; have_storage++) {
      for (use_fallback = 0; use_fallback < 2; use_fallback++) {
        registrations = storage_registrations = starts = 0;
        assert(platform_install_init() == 0);
        platform_install_request_t request = {
            .uri = "http://127.0.0.1:18841/tile.pkg", .display_name = "PKG Manager X",
            .title_name = "PKG Manager X", .content_id = "IV0000-PKGX00001_00-PKGMANAGERX00000",
            .pkg_kind = "base", .category = "gd", .package_size = 6619136
        };
        char content_id[64];
        assert(platform_install_start(&request,content_id,sizeof(content_id),NULL) == 0);
        assert(!strcmp(content_id,request.content_id));
        /* Resolving the optional storage export is allowed, but a URL must
         * never be registered through it (the original 0x8099006A bug). */
        assert(registrations == 1 && storage_registrations == 0 && starts == 1);
        platform_install_close();
        content_id[0] = '\0';
        int rc = platform_install_start_local(&request,
            "/user/data/pkgmgr/dl/PKGX00001-base.pkg", content_id, sizeof(content_id));
        if (have_storage) {
            assert(rc == 0 && !strcmp(content_id, request.content_id));
            assert(storage_registrations == 1 && starts == 2);
        } else {
            assert(rc != 0 && content_id[0] == '\0');
            assert(storage_registrations == 0 && starts == 1);
        }
        assert(registrations == 1); /* local paths never fall back to URL registration */
        platform_install_shutdown();
      }
    }
    puts("PS4 BGFT URL and local registration: passed");
    return 0;
}
