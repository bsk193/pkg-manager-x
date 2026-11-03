#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pkg_parser.h"
#include "pkg_cache.h"
#include "pkg_scanner.h"
#include "app_info.h"
#include "sqlite3.h"
#include "test_fixture.h"

static void write_word(const char *path, uint64_t offset, uint64_t value, int wide) {
    FILE *f = fopen(path, "r+b");
    assert(f && fseeko(f, (off_t)offset, SEEK_SET) == 0);
    uint8_t b[8];
    fx_le64(b, value);
    assert(fwrite(b, 1, wide ? 8 : 4, f) == (size_t)(wide ? 8 : 4));
    assert(fclose(f) == 0);
}

static void expect_sdk(const char *path, uint32_t sdk) {
    pkg_detail_t detail;
    assert(pkg_parser_parse(path, &detail) == 0);
    assert(detail.pkg_type == PKG_TYPE_UPDATE && detail.sdk_version == sdk);
    assert(strcmp(detail.app_version, "v7.04") == 0);
    FILE *f = fopen(path, "rb");
    assert(f && fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size > 0 && size < 1048576 && fseek(f, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)size);
    assert(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    pkg_detail_t mem;
    assert(pkg_parser_parse_mem(bytes, (size_t)size, (uint64_t)size, "synthetic.pkg", &mem, NULL) == 0);
    assert(mem.pkg_type == detail.pkg_type && mem.sdk_version == sdk);
    assert(strcmp(mem.app_version, detail.app_version) == 0);
    free(bytes);
}

int main(void) {
    mkdir("/tmp/mock_smb", 0700);
    char dir[] = "/tmp/mock_smb/ps5-patch-XXXXXX";
    assert(mkdtemp(dir));
    char path[512], settings_path[512], db_path[512];
    snprintf(path, sizeof(path), "%s/image.pkg", dir);
    snprintf(settings_path, sizeof(settings_path), "%s/settings.json", dir);
    snprintf(db_path, sizeof(db_path), "%s/app.db", dir);
    setenv("PKG_SETTINGS_PATH", settings_path, 1);
    setenv("PKG_CACHE_DIR", dir, 1);
    pkg_cache_init();
    assert(fixture_write_ps5_image(path, 1, 0x80000, 0x24000, 0x04000031, 0x40000000) == 0);
    expect_sdk(path, 0x04000031);

    app_settings_t settings = {0};
    settings.smb_share_count = 1;
    settings.smb_shares[0].enabled = 1;
    settings.smb_shares[0].port = 445;
    strcpy(settings.smb_shares[0].server, "mock");
    strcpy(settings.smb_shares[0].share, "pkgs");
    assert(pkg_cache_set_settings(&settings) == 0);
    char url[512];
    snprintf(url, sizeof(url), "smb://mock/pkgs/%s/image.pkg", dir + strlen("/tmp/mock_smb/"));
    pkg_detail_t local, remote, cached;
    assert(pkg_parser_parse(path, &local) == 0);
    assert(pkg_parser_parse(url, &remote) == 0);
    assert(remote.pkg_type == local.pkg_type && remote.sdk_version == local.sdk_version);
    assert(strcmp(remote.app_version, local.app_version) == 0);
    char checksum[64];
    assert(pkg_cache_calc_checksum(path, checksum, sizeof(checksum)) == 0);
    assert(pkg_cache_save(checksum, &local) == 0);
    assert(pkg_cache_lookup(checksum, &cached) == 0 && cached.sdk_version == local.sdk_version);

    assert(fixture_write_ps5_image(path, 1, 0x80000, 0x24000, 0x05000033, 0) == 0);
    expect_sdk(path, 0x05000033); /* LIH itself declares a patch. */
    const uint32_t flags[] = {0x00100000, 0x40000000, 0x41000000, 0x60000000};
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        assert(fixture_write_ps5_image(path, 0, 0x80000, 0x24000, 0x11000040, flags[i]) == 0);
        expect_sdk(path, 0x11000040);
    }
    assert(fixture_write_ps5_image(path, 0, 0x80000, 0x24000, 0x04000031, 0) == 0);
    assert(pkg_parser_parse(path, &local) == 0 && local.pkg_type == PKG_TYPE_BASE && local.sdk_version == 0x04000031);

    assert(fixture_write_ps5_image(path, 1, 0x80000, 0x24000, 0x04000031, 0) == 0);
    write_word(path, FIXTURE_SELF_OFFSET + FIXTURE_SELF_SEGMENT_OFFSET, 0x806, 1);
    expect_sdk(path, 0); /* Encrypted process parameters must not be interpreted. */
    write_word(path, FIXTURE_SELF_OFFSET + FIXTURE_SELF_SEGMENT_OFFSET, 0x80C, 1);
    expect_sdk(path, 0);
    write_word(path, FIXTURE_SELF_OFFSET + FIXTURE_SELF_SEGMENT_OFFSET, 0x804, 1);
    write_word(path, FIXTURE_SELF_OFFSET + 64 + 32, UINT64_MAX, 1);
    expect_sdk(path, 0);
    assert(fixture_write_ps5_image(path, 1, 0x80000, 0x24000, 0xFA000001, 0) == 0);
    expect_sdk(path, 0);
    write_word(path, 0x30, UINT64_MAX, 1);
    assert(pkg_parser_parse(path, &local) != 0);
    assert(pkg_parser_parse(url, &remote) != 0);

    /* Sparse files exercise 64-bit metadata and targeted SDK reads. */
    uint64_t far = (UINT64_C(1) << 32) + 0x24000;
    assert(fixture_write_ps5_image(path, 1, far + 0x10000, far, 0x05000033, 0x40000000) == 0);
    assert(pkg_parser_parse(path, &local) == 0 && local.sdk_version == 0x05000033 && local.pkg_type == PKG_TYPE_UPDATE);
    assert(pkg_parser_parse(url, &remote) == 0 && remote.sdk_version == 0x05000033 && remote.pkg_type == PKG_TYPE_UPDATE);

    sqlite3 *db = NULL;
    assert(sqlite3_open(db_path, &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "CREATE TABLE tbl_contentinfo(titleId TEXT, AppInfoJson TEXT, metaDataPath TEXT, installStatus INTEGER, contentStatus INTEGER, size INTEGER);"
        "INSERT INTO tbl_contentinfo VALUES('PPSA90090', '{\"field_list\":[{\"key\":\"CONTENT_VERSION\",\"data\":\"07.000.003\"}]}', '', 0, 0, 100);", NULL, NULL, NULL) == SQLITE_OK);
    setenv("PKG_APP_DB_PATH", db_path, 1);
    char version[32];
    assert(app_info_check_installed("PPSA90090", version, sizeof(version)) == 1);
    assert(app_info_compare_versions(version, "07.000.003") == 0);
    pkg_install_eligibility_t eligibility;
    pkg_scanner_check_install_eligibility(&local, &eligibility);
    assert(eligibility.can_install);
    assert(sqlite3_exec(db, "UPDATE tbl_contentinfo SET AppInfoJson='{\"field_list\":[{\"key\":\"CONTENT_VERSION\",\"data\":\"07.000.004\"}]}';", NULL, NULL, NULL) == SQLITE_OK);
    pkg_scanner_check_install_eligibility(&local, &eligibility);
    assert(!eligibility.can_install && !strcmp(eligibility.disabled_reason, "Installed version is same or newer"));
    assert(sqlite3_exec(db, "DELETE FROM tbl_contentinfo;", NULL, NULL, NULL) == SQLITE_OK);
    pkg_scanner_check_install_eligibility(&local, &eligibility);
    assert(!eligibility.can_install && !strcmp(eligibility.disabled_reason, "Base package is not installed"));
    sqlite3_close(db);

    /* SDK metadata must survive catalog persistence and reach the API. */
    settings.smb_share_count = 0;
    assert(pkg_cache_set_settings(&settings) == 0);
    setenv("PKG_SCAN_DIR", dir, 1);
    pkg_scanner_init();
    assert(pkg_scanner_scan() == 1);
    assert(pkg_scanner_get_at(0, &cached) == 0 && cached.sdk_version == 0x05000033);
    char *json = pkg_scanner_packages_for_drive_to_json(dir);
    assert(json && strstr(json, "\"sdk_version\":83886131"));
    free(json);
    pkg_scanner_init();
    assert(pkg_scanner_get_at(0, &cached) == 0 && cached.sdk_version == 0x05000033);
    puts("PS5 patch, SDK, cache, SMB and installed-version tests passed");
    return 0;
}
