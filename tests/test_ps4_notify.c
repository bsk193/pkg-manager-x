/*
 * Host test: PS4 notification / user-service policy (ps4_notify.c) and its
 * use by the installer:
 *  - 0x80960003 (user service already initialized) is usable, other codes
 *    stay errors;
 *  - the startup scan popup is exactly "PKG Manager X v<ver>\nFound <n> packages";
 *  - installing the PKG Manager X tile (PKGX00001) on a PS4 sends no manager
 *    "Installing ..." / "... is ready to play!" popups, while other titles
 *    (and every title on PS5) keep them.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "installer.h"
#include "notification.h"
#include "ps4_notify.h"
#include "test_fixture.h"

#define PN_DIR "/tmp/test_ps4_notify"

static void test_user_service_classify(void) {
    assert(ps4_user_service_classify(0) == PS4_USER_SERVICE_READY);
    assert(ps4_user_service_classify((int)0x80960003) == PS4_USER_SERVICE_ALREADY_INITIALIZED);
    assert(ps4_user_service_classify(PKGMGR_USER_SERVICE_ERROR_ALREADY_INITIALIZED) ==
           PS4_USER_SERVICE_ALREADY_INITIALIZED);
    /* Genuine failures are not swallowed: neighbouring user-service codes,
     * generic errors and positive values. */
    const int errors[] = { (int)0x80960001, (int)0x80960002, (int)0x80960004,
                           (int)0x80960009, (int)0x8096000A, -1, 1, (int)0x80020002 };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        assert(ps4_user_service_classify(errors[i]) == PS4_USER_SERVICE_FAILED);
    }
    printf("  user service: already-initialized benign, other codes errors ok\n");
}

static int count_lines(const char *s) {
    int n = 1;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

static void test_scan_popup(void) {
    char msg[160];
    ps4_format_scan_notification(msg, sizeof(msg), "1.0.1", 42);
    assert(strcmp(msg, "PKG Manager X v1.0.1\nFound 42 packages") == 0);
    assert(count_lines(msg) == 2);
    assert(!strstr(msg, "http") && !strstr(msg, "Port") && !strstr(msg, ":8844"));

    ps4_format_scan_notification(msg, sizeof(msg), "1.0.0-3-gabc1234", 0);
    assert(strcmp(msg, "PKG Manager X v1.0.0-3-gabc1234\nFound 0 packages") == 0);
    ps4_format_scan_notification(msg, sizeof(msg), "2.3.4", 12345);
    assert(strcmp(msg, "PKG Manager X v2.3.4\nFound 12345 packages") == 0);

    char tiny[8];
    ps4_format_scan_notification(tiny, sizeof(tiny), "1.0.0", 7);
    assert(strlen(tiny) == sizeof(tiny) - 1); /* truncated, still terminated */
    printf("  scan popup: two lines, dynamic version/count, no address ok\n");
}

static void test_tile_policy(void) {
    assert(ps4_is_manager_tile("PKGX00001"));
    assert(ps4_is_manager_tile("pkgx00001"));
    assert(!ps4_is_manager_tile("PKGX00002"));
    assert(!ps4_is_manager_tile("CUSA00001"));
    assert(!ps4_is_manager_tile(""));
    assert(!ps4_is_manager_tile(NULL));

    assert(ps4_suppress_install_notification("ps4", "PKGX00001"));
    assert(!ps4_suppress_install_notification("ps5", "PKGX00001")); /* PS5 unchanged */
    assert(!ps4_suppress_install_notification("ps4", "CUSA00001"));
    assert(!ps4_suppress_install_notification(NULL, "PKGX00001"));
    printf("  tile policy: only PKGX00001 on PS4 ok\n");
}

static int wait_done(int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 50) {
        installer_status_t st;
        installer_get_status(&st);
        if (!st.is_installing && (st.completed || st.failed)) return 0;
        usleep(50 * 1000);
    }
    return -1;
}

static int notified(const char *needle) {
    for (int i = 0; i < notification_test_count(); i++) {
        const char *m = notification_test_get(i);
        if (m && strstr(m, needle)) return 1;
    }
    return 0;
}

static void install_and_wait(const char *path) {
    notification_test_reset();
    int rc = installer_start(path);
    if (rc != 0) printf("  installer_start(%s) = %d\n", path, rc);
    assert(rc == 0);
    assert(wait_done(15000) == 0);
    usleep(300 * 1000); /* the completion popup is sent right after the state change */
    installer_status_t st;
    installer_get_status(&st);
    assert(st.completed == 1);
}

static void test_installer_popups(void) {
    setenv("PKGMGR_CONSOLE", "ps4", 1);
    assert(installer_init("http://127.0.0.1:8844/") == 0);

    /* The tile itself: no manager start / ready popups, status still complete. */
    assert(fixture_write_ps4_pkg(PN_DIR "/tile.pkg", PKGMGR_TILE_TITLE_ID, "PKG Manager X", "gd", "01.01") == 0);
    install_and_wait(PN_DIR "/tile.pkg");
    assert(!notified("Installing PKG Manager X"));
    assert(!notified("is ready to play"));
    char *json = installer_status_to_json();
    assert(json && strstr(json, "PKG Manager X is ready to play!")); /* web UI still says it */
    free(json);

    /* Any other title keeps both popups. */
    assert(fixture_write_ps4_pkg(PN_DIR "/game.pkg", "CUSA90001", "Other Game", "gd", "01.00") == 0);
    install_and_wait(PN_DIR "/game.pkg");
    assert(notified("Installing Other Game..."));
    assert(notified("Other Game is ready to play!"));

    installer_shutdown();
    unsetenv("PKGMGR_CONSOLE");
    printf("  installer: tile popups skipped on PS4, other titles unchanged ok\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("==============================================\n");
    printf(">>> RUNNING PS4 NOTIFICATION POLICY TEST <<<\n");
    printf("==============================================\n");
    assert(system("rm -rf " PN_DIR " && mkdir -p " PN_DIR "/cache") == 0);
    setenv("PKG_CACHE_DIR", PN_DIR "/cache", 1);
    setenv("PKG_SETTINGS_PATH", PN_DIR "/settings.json", 1);
    setenv("PKG_HTTP_SOURCES_PATH", PN_DIR "/http_sources.json", 1);
    setenv("PKG_TMP_DIR", PN_DIR "/tmp", 1);
    setenv("PKG_LOG_FILE", "none", 1);

    test_user_service_classify();
    test_scan_popup();
    test_tile_policy();
    test_installer_popups();

    printf("\n>>> ALL PS4 NOTIFICATION POLICY TESTS PASSED! <<<\n");
    return 0;
}
