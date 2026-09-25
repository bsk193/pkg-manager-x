/*
 * Host test: package platform tags (pkg_platform.c) and their use by the
 * scanner: parser evidence, title-ID / folder fallbacks, folder mismatch,
 * PS4/PS5 sub-folder scanning, and PS4-console install gating.
 *
 *   ./test_pkg_platform          emulates a PS5 (installs everything)
 *   ./test_pkg_platform --ps4    run with PKGMGR_CONSOLE=ps4 (PS5 pkgs refused)
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pkg_parser.h"
#include "pkg_platform.h"
#include "pkg_scanner.h"
#include "test_fixture.h"

#define PT_DIR "/tmp/test_pkg_platform"

static void test_parser_evidence(void) {
    pkg_detail_t d;
    assert(fixture_write_ps4_pkg(PT_DIR "/four.pkg", "CUSA92001", "Four", "gd", "01.00") == 0);
    assert(pkg_parser_parse(PT_DIR "/four.pkg", &d) == 0);
    assert(strcmp(d.platform, "ps4") == 0);

    assert(fixture_write_ps5_pkg(PT_DIR "/five.pkg", "PPSA92002", "Five", "gd", "01.000.000", 0) == 0);
    assert(pkg_parser_parse(PT_DIR "/five.pkg", &d) == 0);
    assert(strcmp(d.platform, "ps5") == 0);

    /* Homebrew-style ID: only the FIH container says PS5. */
    assert(fixture_write_ps5_pkg(PT_DIR "/hb.pkg", "HBRW00001", "Homebrew", "gd", "01.000.000", 0) == 0);
    assert(pkg_parser_parse(PT_DIR "/hb.pkg", &d) == 0);
    assert(strcmp(d.platform, "ps5") == 0);
    printf("  parser evidence ok\n");
}

static void test_fallbacks(void) {
    pkg_detail_t d;
    memset(&d, 0, sizeof(d));
    snprintf(d.title_id, sizeof(d.title_id), "PPSA00001");
    pkg_platform_finalize(&d);
    assert(strcmp(d.platform, "ps5") == 0);

    memset(&d, 0, sizeof(d));
    snprintf(d.title_id, sizeof(d.title_id), "CUSA00001");
    pkg_platform_finalize(&d);
    assert(strcmp(d.platform, "ps4") == 0);

    memset(&d, 0, sizeof(d));
    snprintf(d.title_id, sizeof(d.title_id), "LAPY20001");
    snprintf(d.path, sizeof(d.path), "/mnt/usb0/PS4 Games/store.pkg");
    pkg_platform_finalize(&d);
    assert(strcmp(d.platform, "ps4") == 0);

    memset(&d, 0, sizeof(d));
    snprintf(d.title_id, sizeof(d.title_id), "LAPY20001");
    snprintf(d.path, sizeof(d.path), "/mnt/usb0/games/store.pkg");
    pkg_platform_finalize(&d);
    assert(d.platform[0] == '\0');

    /* Parser evidence is never overridden. */
    memset(&d, 0, sizeof(d));
    snprintf(d.platform, sizeof(d.platform), "ps5");
    snprintf(d.title_id, sizeof(d.title_id), "CUSA00001");
    pkg_platform_finalize(&d);
    assert(strcmp(d.platform, "ps5") == 0);
    printf("  title-id / folder fallbacks ok\n");
}

static void test_folder_hints(void) {
    assert(strcmp(pkg_platform_folder_hint("/mnt/usb0/PS5/Games/x.pkg"), "ps5") == 0);
    assert(strcmp(pkg_platform_folder_hint("/mnt/usb0/ps4_backups/x.pkg"), "ps4") == 0);
    assert(strcmp(pkg_platform_folder_hint("/mnt/usb0/PS4/PS5/x.pkg"), "ps5") == 0);
    assert(strcmp(pkg_platform_folder_hint("/mnt/usb0/PS45/x.pkg"), "") == 0);
    assert(strcmp(pkg_platform_folder_hint("/mnt/usb0/ps4"), "") == 0);          /* file, not folder */
    assert(strcmp(pkg_platform_folder_hint("http://ps4/pkgs/x.pkg"), "") == 0);  /* host ignored */
    assert(strcmp(pkg_platform_folder_hint("http://nas/PS4/x.pkg"), "ps4") == 0);
    assert(strcmp(pkg_platform_folder_hint("smb://nas/PS5/x.pkg"), "ps5") == 0);
    assert(strcmp(pkg_platform_folder_name("PS5 Games"), "ps5") == 0);
    assert(strcmp(pkg_platform_folder_name("pkg"), "") == 0);

    pkg_detail_t d;
    memset(&d, 0, sizeof(d));
    snprintf(d.platform, sizeof(d.platform), "ps5");
    snprintf(d.path, sizeof(d.path), "/mnt/usb0/PS4/wrong.pkg");
    assert(pkg_platform_folder_mismatch(&d) == 1);
    snprintf(d.path, sizeof(d.path), "/mnt/usb0/PS5/right.pkg");
    assert(pkg_platform_folder_mismatch(&d) == 0);
    printf("  folder hints / mismatch ok\n");
}

static void test_gating(int ps4) {
    pkg_detail_t d;
    const char *reason = NULL;
    memset(&d, 0, sizeof(d));
    assert(strcmp(pkg_platform_console(), ps4 ? "ps4" : "ps5") == 0);

    snprintf(d.platform, sizeof(d.platform), "ps4");
    assert(pkg_platform_can_install(&d, &reason) == 1);

    snprintf(d.platform, sizeof(d.platform), "ps5");
    if (ps4) {
        assert(pkg_platform_can_install(&d, &reason) == 0);
        assert(strcmp(reason, PKG_PLATFORM_REASON_PS5_ON_PS4) == 0);
    } else {
        assert(pkg_platform_can_install(&d, &reason) == 1);
    }

    d.platform[0] = '\0';
    assert(pkg_platform_can_install(&d, &reason) == 1);
    printf("  install gating ok (%s console)\n", ps4 ? "PS4" : "PS5");
}

static void test_scanner_folders(int ps4) {
    assert(system("mkdir -p " PT_DIR "/usb0/PS4 " PT_DIR "/usb0/ps5/nested " PT_DIR "/usb0/other") == 0);
    assert(fixture_write_ps4_pkg(PT_DIR "/usb0/PS4/Four.pkg", "CUSA92101", "Folder Four", "gd", "01.00") == 0);
    assert(fixture_write_ps5_pkg(PT_DIR "/usb0/ps5/nested/Five.pkg", "PPSA92102", "Folder Five", "gd",
                                 "01.000.000", 0) == 0);
    /* PS5 package stored in the PS4 folder -> mismatch flag. */
    assert(fixture_write_ps5_pkg(PT_DIR "/usb0/PS4/Misplaced.pkg", "PPSA92103", "Misplaced", "gd",
                                 "01.000.000", 0) == 0);
    /* Outside root / pkg / PS4 / PS5: not scanned (upstream rule). */
    assert(fixture_write_ps4_pkg(PT_DIR "/usb0/other/Hidden.pkg", "CUSA92104", "Hidden", "gd", "01.00") == 0);

    pkg_scanner_init();
    pkg_scanner_scan();
    char *json = pkg_scanner_packages_for_drive_to_json("usb0");
    assert(json);
    assert(strstr(json, "CUSA92101") && strstr(json, "PPSA92102") && strstr(json, "PPSA92103"));
    assert(!strstr(json, "CUSA92104"));
    assert(strstr(json, "\"platform\":\"ps5\",\"folder_platform\":\"ps4\",\"platform_mismatch\":true"));
    if (ps4) {
        assert(strstr(json, PKG_PLATFORM_REASON_PS5_ON_PS4));
    } else {
        assert(!strstr(json, PKG_PLATFORM_REASON_PS5_ON_PS4));
    }
    free(json);
    printf("  scanner PS4/PS5 folders ok\n");
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    int ps4 = argc > 1 && strcmp(argv[1], "--ps4") == 0;
    if (ps4) setenv("PKGMGR_CONSOLE", "ps4", 1);
    else unsetenv("PKGMGR_CONSOLE");

    printf("==============================================\n");
    printf(">>> RUNNING PKG PLATFORM TEST (%s) <<<\n", ps4 ? "PS4" : "PS5");
    printf("==============================================\n");
    assert(system("rm -rf " PT_DIR " && mkdir -p " PT_DIR "/cache") == 0);
    setenv("PKG_CACHE_DIR", PT_DIR "/cache", 1);
    setenv("PKG_SETTINGS_PATH", PT_DIR "/settings.json", 1);
    setenv("PKG_HTTP_SOURCES_PATH", PT_DIR "/http_sources.json", 1);
    setenv("PKG_USB_PREFIX", PT_DIR "/usb", 1);
    setenv("PKG_DISC_DIR", PT_DIR "/no_disc", 1);
    setenv("PKG_LOG_FILE", "none", 1);

    test_parser_evidence();
    test_fallbacks();
    test_folder_hints();
    test_gating(ps4);
    test_scanner_folders(ps4);

    printf("\n>>> ALL PKG PLATFORM TESTS PASSED! <<<\n");
    return 0;
}
