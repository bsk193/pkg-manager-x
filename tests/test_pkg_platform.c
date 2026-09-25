/*
 * Host test: package platform / content type (pkg_platform.c) and their use
 * by the scanner. Classification comes from package metadata only; the
 * folder a package sits in never matters.
 *
 *   ./test_pkg_platform          emulates a PS5
 *   ./test_pkg_platform --ps4    run with PKGMGR_CONSOLE=ps4
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

static void test_title_id_fallback(void) {
    pkg_detail_t d;
    memset(&d, 0, sizeof(d));
    snprintf(d.title_id, sizeof(d.title_id), "PPSA00001");
    pkg_platform_finalize(&d);
    assert(strcmp(d.platform, "ps5") == 0);

    memset(&d, 0, sizeof(d));
    snprintf(d.title_id, sizeof(d.title_id), "PLJM00001");
    pkg_platform_finalize(&d);
    assert(strcmp(d.platform, "ps4") == 0);

    /* Folder names never classify: a homebrew ID in a "PS4 Games" folder
     * stays unknown when the package itself says nothing. */
    memset(&d, 0, sizeof(d));
    snprintf(d.title_id, sizeof(d.title_id), "LAPY20001");
    snprintf(d.path, sizeof(d.path), "/mnt/usb0/PS4 Games/store.pkg");
    pkg_platform_finalize(&d);
    assert(d.platform[0] == '\0');

    /* Parser evidence is never overridden. */
    memset(&d, 0, sizeof(d));
    snprintf(d.platform, sizeof(d.platform), "ps5");
    snprintf(d.title_id, sizeof(d.title_id), "CUSA00001");
    pkg_platform_finalize(&d);
    assert(strcmp(d.platform, "ps5") == 0);

    assert(strcmp(pkg_platform_folder_name("PS5 Games"), "ps5") == 0);
    assert(strcmp(pkg_platform_folder_name("pkg"), "") == 0);
    printf("  title-id fallback ok (no folder fallback)\n");
}

static pkg_detail_t mk(const char *platform, const char *tid, const char *cid, pkg_type_t type) {
    pkg_detail_t d;
    memset(&d, 0, sizeof(d));
    snprintf(d.platform, sizeof(d.platform), "%s", platform);
    snprintf(d.title_id, sizeof(d.title_id), "%s", tid);
    snprintf(d.content_id, sizeof(d.content_id), "%s", cid);
    d.pkg_type = type;
    return d;
}

static void test_content_type(void) {
    pkg_detail_t d = mk("ps4", "CUSA00001", "UP0000-CUSA00001_00-GAME000000000000", PKG_TYPE_BASE);
    assert(strcmp(pkg_platform_content_type(&d), "game") == 0);
    d.pkg_type = PKG_TYPE_UPDATE;
    assert(strcmp(pkg_platform_content_type(&d), "update") == 0);
    d.pkg_type = PKG_TYPE_DLC;
    assert(strcmp(pkg_platform_content_type(&d), "dlc") == 0);

    /* Non-retail title ID -> homebrew. */
    d = mk("ps4", "LAPY20001", "", PKG_TYPE_BASE);
    assert(pkg_platform_is_homebrew(&d));
    assert(strcmp(pkg_platform_content_type(&d), "homebrew") == 0);
    /* Fake-signed publisher IV0000 -> homebrew, even with a retail-like ID. */
    d = mk("ps4", "CUSA99999", "IV0000-CUSA99999_00-HOMEBREW00000000", PKG_TYPE_BASE);
    assert(pkg_platform_is_homebrew(&d));
    /* Add-ons and patches are never homebrew. */
    d = mk("ps4", "LAPY20001", "IV0000-LAPY20001_00-ADDON00000000000", PKG_TYPE_DLC);
    assert(!pkg_platform_is_homebrew(&d));
    assert(strcmp(pkg_platform_content_type(&d), "dlc") == 0);
    /* Retail titles, all regions / consoles. */
    const char *retail[] = { "CUSA00001", "PLAS00001", "PCJS00001", "PPSA00001", "ELJM00001" };
    for (size_t i = 0; i < sizeof(retail) / sizeof(retail[0]); i++) {
        d = mk("", retail[i], "", PKG_TYPE_BASE);
        assert(!pkg_platform_is_homebrew(&d));
    }
    /* Unknown / fallback IDs are not guessed to be homebrew. */
    d = mk("", "UNKNOWN", "", PKG_TYPE_BASE);
    assert(!pkg_platform_is_homebrew(&d));
    printf("  content types / homebrew ok\n");
}

static void test_gating(int ps4) {
    const char *reason = NULL;
    assert(strcmp(pkg_platform_console(), ps4 ? "ps4" : "ps5") == 0);

    pkg_detail_t game4 = mk("ps4", "CUSA00001", "", PKG_TYPE_BASE);
    pkg_detail_t dlc4 = mk("ps4", "CUSA00001", "", PKG_TYPE_DLC);
    pkg_detail_t patch4 = mk("ps4", "CUSA00001", "", PKG_TYPE_UPDATE);
    pkg_detail_t hb4 = mk("ps4", "LAPY20001", "", PKG_TYPE_BASE);
    pkg_detail_t game5 = mk("ps5", "PPSA00001", "", PKG_TYPE_BASE);
    pkg_detail_t hb5 = mk("ps5", "HBRW00001", "", PKG_TYPE_BASE);
    pkg_detail_t unknown = mk("", "UNKNOWN", "", PKG_TYPE_BASE);

    assert(pkg_platform_can_install(&unknown, &reason) == 1);
    if (ps4) {
        assert(pkg_platform_can_install(&game4, &reason) == 1);
        assert(pkg_platform_can_install(&hb4, &reason) == 1);
        assert(pkg_platform_can_install(&game5, &reason) == 0);
        assert(strcmp(reason, PKG_PLATFORM_REASON_PS5_ONLY) == 0);
        assert(pkg_platform_can_install(&hb5, &reason) == 0);
        /* The PS4-on-PS5 switch does not matter on a PS4. */
        pkg_platform_set_allow_ps4_on_ps5(0);
        assert(pkg_platform_can_install(&game4, &reason) == 1);
        pkg_platform_set_allow_ps4_on_ps5(1);
    } else {
        assert(pkg_platform_can_install(&game5, &reason) == 1);
        assert(pkg_platform_can_install(&hb5, &reason) == 1);
        assert(pkg_platform_can_install(&game4, &reason) == 1);
        assert(pkg_platform_can_install(&dlc4, &reason) == 1);
        assert(pkg_platform_can_install(&patch4, &reason) == 1);
        assert(pkg_platform_can_install(&hb4, &reason) == 0);
        assert(strcmp(reason, PKG_PLATFORM_REASON_PS4_HOMEBREW) == 0);

        pkg_platform_set_allow_ps4_on_ps5(0);
        assert(pkg_platform_can_install(&game4, &reason) == 0);
        assert(strcmp(reason, PKG_PLATFORM_REASON_PS4_DISABLED) == 0);
        assert(pkg_platform_can_install(&game5, &reason) == 1);
        pkg_platform_set_allow_ps4_on_ps5(1);
    }
    printf("  install gating ok (%s console)\n", ps4 ? "PS4" : "PS5");
}

/* Copies the JSON object of the package with this title ID. */
static void pkg_obj(const char *json, const char *title_id, char *out, size_t out_sz) {
    char key[64];
    snprintf(key, sizeof(key), "\"title_id\":\"%s\"", title_id);
    const char *hit = strstr(json, key);
    assert(hit);
    const char *start = hit;
    while (start > json && strncmp(start, "{\"path\"", 7) != 0) start--;
    const char *end = strstr(hit, "\"unavailable\":");
    assert(end);
    end = strchr(end, '}');
    assert(end);
    size_t n = (size_t)(end - start + 1);
    assert(n < out_sz);
    memcpy(out, start, n);
    out[n] = '\0';
}

static void test_scanner(int ps4) {
    assert(system("mkdir -p " PT_DIR "/usb0/PS4/games " PT_DIR "/usb0/ps5/nested " PT_DIR "/usb0/other") == 0);
    assert(fixture_write_ps4_pkg(PT_DIR "/usb0/PS4/Four.pkg", "CUSA92101", "Folder Four", "gd", "01.00") == 0);
    assert(fixture_write_ps5_pkg(PT_DIR "/usb0/ps5/nested/Five.pkg", "PPSA92102", "Folder Five", "gd",
                                 "01.000.000", 0) == 0);
    /* PS5 package in the PS4 folder: still PS5 (the folder means nothing). */
    assert(fixture_write_ps5_pkg(PT_DIR "/usb0/PS4/Misplaced.pkg", "PPSA92103", "Misplaced", "gd",
                                 "01.000.000", 0) == 0);
    /* DLC stored inside games/: still a DLC. */
    assert(fixture_write_ps4_pkg(PT_DIR "/usb0/PS4/games/Addon.pkg", "CUSA92105", "Addon", "ac", "01.00") == 0);
    /* PS4 homebrew. */
    assert(fixture_write_ps4_pkg(PT_DIR "/usb0/PS4/games/Store.pkg", "LAPY92106", "Store", "gd", "01.00") == 0);
    /* Outside root / pkg / PS4 / PS5: not scanned (upstream rule). */
    assert(fixture_write_ps4_pkg(PT_DIR "/usb0/other/Hidden.pkg", "CUSA92104", "Hidden", "gd", "01.00") == 0);

    pkg_scanner_init();
    pkg_scanner_scan();
    char *json = pkg_scanner_packages_for_drive_to_json("usb0");
    assert(json);
    assert(!strstr(json, "CUSA92104"));
    assert(!strstr(json, "folder_platform") && !strstr(json, "platform_mismatch"));

    char o[8192];
    pkg_obj(json, "PPSA92103", o, sizeof(o));
    assert(strstr(o, "\"platform\":\"ps5\",\"content_type\":\"game\""));
    pkg_obj(json, "CUSA92105", o, sizeof(o));
    assert(strstr(o, "\"platform\":\"ps4\",\"content_type\":\"dlc\""));
    pkg_obj(json, "LAPY92106", o, sizeof(o));
    assert(strstr(o, "\"content_type\":\"homebrew\""));

    if (ps4) {
        /* PS4 + local source: PS4 installable, PS5 greyed "PS5 only". */
        pkg_obj(json, "CUSA92101", o, sizeof(o));
        assert(strstr(o, "\"platform_blocked\":false"));
        pkg_obj(json, "LAPY92106", o, sizeof(o));
        assert(strstr(o, "\"platform_blocked\":false"));
        pkg_obj(json, "PPSA92102", o, sizeof(o));
        assert(strstr(o, "\"platform_blocked\":true,\"platform_reason\":\"" PKG_PLATFORM_REASON_PS5_ONLY "\""));
        assert(strstr(o, "\"can_install\":false"));
    } else {
        /* PS5: PS5 + PS4 game/DLC installable, PS4 homebrew greyed. */
        pkg_obj(json, "PPSA92102", o, sizeof(o));
        assert(strstr(o, "\"platform_blocked\":false"));
        pkg_obj(json, "CUSA92101", o, sizeof(o));
        assert(strstr(o, "\"platform_blocked\":false"));
        pkg_obj(json, "LAPY92106", o, sizeof(o));
        assert(strstr(o, "\"platform_blocked\":true,\"platform_reason\":\"" PKG_PLATFORM_REASON_PS4_HOMEBREW "\""));
        assert(strstr(o, "\"can_install\":false"));
        free(json);

        /* Settings switch off: PS4 titles greyed with their own reason. */
        pkg_platform_set_allow_ps4_on_ps5(0);
        json = pkg_scanner_packages_for_drive_to_json("usb0");
        pkg_obj(json, "CUSA92101", o, sizeof(o));
        assert(strstr(o, "\"platform_reason\":\"" PKG_PLATFORM_REASON_PS4_DISABLED "\""));
        pkg_obj(json, "PPSA92102", o, sizeof(o));
        assert(strstr(o, "\"platform_blocked\":false"));
        pkg_platform_set_allow_ps4_on_ps5(1);
    }
    free(json);
    printf("  scanner classification ok\n");
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
    test_title_id_fallback();
    test_content_type();
    test_gating(ps4);
    test_scanner(ps4);

    printf("\n>>> ALL PKG PLATFORM TESTS PASSED! <<<\n");
    return 0;
}
