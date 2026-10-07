/*
 * Host test: PS4 (param.sfo) title localization.
 *
 * Resident Evil 2 DLC packages (e.g. "Leon Costume: \"Noir\"",
 * EP0102-CUSA09171_00-BH20000COSDLC002) carry English only in TITLE; their
 * localized entries start with TITLE_00 (Japanese) and include no English
 * TITLE_xx. The titles below are the ones read from that package. An English
 * browser used to get the Japanese title. Checks:
 *  - SFO parsing keeps TITLE as the default title;
 *  - pkg_parser_resolve_sfo_title: requested-language match, else TITLE;
 *    a failed resolution never blanks the title;
 *  - the scanner JSON (fresh scan and reloaded from the cached manifest)
 *    shows the right title for PS4, while PS5 packages keep the generic
 *    resolver.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pkg_parser.h"
#include "pkg_scanner.h"
#include "test_fixture.h"

#define TL_DIR "/tmp/test_ps4_title_localization"

#define NOIR_EN "Leon Costume: \"Noir\""
#define NOIR_JA "コスチューム 「レオン・NOIR」"
#define NOIR_DE "Leon-Kostüm: \"Film noir\""
#define NOIR_PT_BR "Traje do Leon: \"Noir\""

static const char *const k_noir_keys[] = { "TITLE_00", "TITLE_02", "TITLE_04", "TITLE_17" };
static const char *const k_noir_vals[] = { NOIR_JA, "Tenue pour Leon : \"Film noir\"", NOIR_DE, NOIR_PT_BR };

static void parse_noir(char *title, size_t title_sz, char *loc, size_t loc_sz, char *def, size_t def_sz) {
    uint8_t sfo[4096];
    size_t n = fixture_build_sfo_ex(sfo, sizeof(sfo), NOIR_EN, "CUSA09171", "ac", "01.00",
                                    k_noir_keys, k_noir_vals, 4);
    assert(n > 0);
    char tid[32] = "", ver[32] = "", cat[16] = "";
    title[0] = loc[0] = def[0] = '\0';
    pkg_parser_parse_param_sfo(sfo, n, title, title_sz, tid, sizeof(tid), ver, sizeof(ver),
                               cat, sizeof(cat), loc, loc_sz, def, def_sz);
    assert(strcmp(tid, "CUSA09171") == 0 && strcmp(cat, "ac") == 0);
}

static void expect_sfo(const char *loc, const char *accept, const char *def_title, const char *want) {
    char out[PKG_TITLE_NAME_LEN];
    int rc = pkg_parser_resolve_sfo_title(loc, accept, def_title, out, sizeof(out));
    if (rc != 0 || strcmp(out, want) != 0) {
        printf("  accept=[%s]: got rc=%d [%s], want [%s]\n", accept ? accept : "(null)", rc, out, want);
    }
    assert(rc == 0 && strcmp(out, want) == 0);
}

static void test_parse_and_resolve(void) {
    char title[PKG_TITLE_NAME_LEN], loc[PKG_LOCALIZED_TITLES_LEN], def[32];
    parse_noir(title, sizeof(title), loc, sizeof(loc), def, sizeof(def));
    assert(strcmp(title, NOIR_EN) == 0);                 /* TITLE stays the default */
    assert(strncmp(loc, "{\"ja-JP\":", 9) == 0);        /* Japanese is the first entry */
    assert(!strstr(loc, "\"en-"));                       /* no English TITLE_xx at all */

    /* English (or any language without a TITLE_xx) -> TITLE, never TITLE_00. */
    expect_sfo(loc, "en-US,en;q=0.9", title, NOIR_EN);
    expect_sfo(loc, "en", title, NOIR_EN);
    expect_sfo(loc, "en-GB", title, NOIR_EN);
    expect_sfo(loc, "nl-NL,nl;q=0.9", title, NOIR_EN);
    expect_sfo(loc, "", title, NOIR_EN);
    expect_sfo(loc, NULL, title, NOIR_EN);
    /* Languages the package has keep their translation. */
    expect_sfo(loc, "ja-JP", title, NOIR_JA);
    expect_sfo(loc, "de-DE,de;q=0.9,en;q=0.8", title, NOIR_DE);
    expect_sfo(loc, "pt-PT,pt;q=0.9,en;q=0.8", title, NOIR_PT_BR); /* primary-language match */
    /* Quality-ordered lists: the first listed language with a title wins. */
    expect_sfo(loc, "en-US,de;q=0.5", title, NOIR_DE);

    /* Localization failure keeps a valid default title. */
    expect_sfo(NULL, "fr-FR", NOIR_EN, NOIR_EN);
    expect_sfo("{}", "fr-FR", NOIR_EN, NOIR_EN);
    expect_sfo("not json", "en", NOIR_EN, NOIR_EN);
    /* Without TITLE: English if present, else any localized title, never "". */
    expect_sfo("{\"ja-JP\":\"J\",\"en-GB\":\"E\"}", "nl", "", "E");
    expect_sfo(loc, "nl", "", NOIR_JA);
    char out[PKG_TITLE_NAME_LEN] = "x";
    assert(pkg_parser_resolve_sfo_title("{}", "en", "", out, sizeof(out)) != 0 && out[0] == '\0');
    assert(pkg_parser_resolve_sfo_title(NULL, NULL, NULL, out, sizeof(out)) != 0 && out[0] == '\0');

    /* The generic resolver (PS5 param.json path) is unchanged. */
    char generic[PKG_TITLE_NAME_LEN];
    assert(pkg_parser_resolve_localized_title(loc, def, "de-DE", generic, sizeof(generic)) == 0);
    assert(strcmp(generic, NOIR_DE) == 0);
    printf("  SFO parse + PS4 title resolution ok\n");
}

/* Copies the JSON object of the package whose content ID is cid. */
static void pkg_obj(const char *json, const char *cid, char *out, size_t out_sz) {
    char key[96];
    snprintf(key, sizeof(key), "\"content_id\":\"%s\"", cid);
    const char *hit = strstr(json, key);
    if (!hit) printf("  missing %s in %s\n", cid, json);
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

#define NOIR_CID "EP0102-CUSA09171_00-BH20000COSDLC002"
#define PS5_CID "EP0001-PPSA93001_00-TEST000000000001"

static void expect_title(const char *accept, const char *cid, const char *want_json_escaped) {
    char *json = pkg_scanner_packages_for_drive_to_json_ex("usb0", accept);
    assert(json);
    char obj[16384];
    pkg_obj(json, cid, obj, sizeof(obj));
    char want[512];
    snprintf(want, sizeof(want), "\"title_name\":\"%s\"", want_json_escaped);
    if (!strstr(obj, want)) printf("  accept=[%s] %s: want %s in\n  %s\n", accept, cid, want, obj);
    assert(strstr(obj, want));
    free(json);
}

static void check_scanner_titles(const char *label) {
    expect_title("en-US,en;q=0.9", NOIR_CID, "Leon Costume: \\\"Noir\\\"");
    expect_title("nl-NL", NOIR_CID, "Leon Costume: \\\"Noir\\\"");
    expect_title("ja-JP", NOIR_CID, NOIR_JA);
    expect_title("pt-PT,pt;q=0.9", NOIR_CID, "Traje do Leon: \\\"Noir\\\"");
    /* PS5 (param.json): generic resolver, defaultLanguage pl-PL wins for nl. */
    expect_title("en-US", PS5_CID, "English Title");
    expect_title("nl-NL", PS5_CID, "Polish Title");
    printf("  scanner titles ok (%s)\n", label);
}

static void test_scanner(void) {
    assert(system("mkdir -p " TL_DIR "/usb0/PS4/dlcs " TL_DIR "/usb0/PS5") == 0);
    fixture_pkg_spec_t s;
    memset(&s, 0, sizeof(s));
    s.title_id = "CUSA09171";
    s.title = NOIR_EN;
    s.category = "ac";
    s.version = "01.00";
    s.content_id = NOIR_CID;
    s.sfo_extra_keys = k_noir_keys;
    s.sfo_extra_vals = k_noir_vals;
    s.sfo_extra_count = 4;
    assert(fixture_write_pkg(TL_DIR "/usb0/PS4/dlcs/Leon_Costume_Noir.pkg", 0, &s) == 0);
    assert(fixture_write_ps5_pkg_multilang(TL_DIR "/usb0/PS5/Multi.pkg", "PPSA93001", "pl-PL", "gd",
                                           "01.000.000", 0) == 0);

    pkg_scanner_init();
    pkg_scanner_scan();
    check_scanner_titles("fresh scan");

    /* Restart: entries now come from the cached manifest written above. */
    pkg_scanner_init();
    check_scanner_titles("cached manifest");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("==============================================\n");
    printf(">>> RUNNING PS4 TITLE LOCALIZATION TEST <<<\n");
    printf("==============================================\n");
    assert(system("rm -rf " TL_DIR " && mkdir -p " TL_DIR "/cache") == 0);
    setenv("PKG_CACHE_DIR", TL_DIR "/cache", 1);
    setenv("PKG_SETTINGS_PATH", TL_DIR "/settings.json", 1);
    setenv("PKG_HTTP_SOURCES_PATH", TL_DIR "/http_sources.json", 1);
    setenv("PKG_USB_PREFIX", TL_DIR "/usb", 1);
    setenv("PKG_DISC_DIR", TL_DIR "/no_disc", 1);
    setenv("PKG_LOG_FILE", "none", 1);

    test_parse_and_resolve();
    test_scanner();

    printf("\n>>> ALL PS4 TITLE LOCALIZATION TESTS PASSED! <<<\n");
    return 0;
}
