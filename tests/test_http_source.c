/*
 * Host test: HTTP/HTTPS package sources (http_source.c) against the local
 * test server: discovery (HTML listing + index.json), metadata parsing,
 * pooled range streaming, auth, redirects, servers without Range support,
 * and scanner integration (drive type "http", platform tags).
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "http_source.h"
#include "http_test_server.h"
#include "multipart.h"
#include "pkg_scanner.h"
#include "test_fixture.h"

#define HT_DIR "/tmp/test_http_source"
#define HT_ROOT HT_DIR "/www"

static char g_base[128];

typedef struct {
    int count;
    char urls[16][512];
    char names[16][256];
    uint64_t sizes[16];
} found_t;

static void collect_cb(const char *url, const char *filename, uint64_t size, uint32_t mtime, void *user) {
    (void)mtime;
    found_t *f = (found_t *)user;
    if (f->count < 16) {
        snprintf(f->urls[f->count], sizeof(f->urls[0]), "%s", url);
        snprintf(f->names[f->count], sizeof(f->names[0]), "%s", filename);
        f->sizes[f->count] = size;
    }
    f->count++;
}

static uint64_t file_size(const char *p) {
    struct stat st;
    assert(stat(p, &st) == 0);
    return (uint64_t)st.st_size;
}

static int find_name(const found_t *f, const char *name) {
    for (int i = 0; i < f->count && i < 16; i++) {
        if (strcmp(f->names[i], name) == 0) return i;
    }
    return -1;
}

static void make_config(http_source_config_t *c, const char *user, const char *pass) {
    memset(c, 0, sizeof(*c));
    c->enabled = 1;
    snprintf(c->url, sizeof(c->url), "%s", g_base);
    snprintf(c->label, sizeof(c->label), "Test HTTP");
    if (user) snprintf(c->username, sizeof(c->username), "%s", user);
    if (pass) snprintf(c->password, sizeof(c->password), "%s", pass);
}

static void setup_tree(void) {
    assert(system("rm -rf " HT_DIR " && mkdir -p " HT_ROOT "/PS4 " HT_ROOT "/PS5/Deep " HT_DIR "/cache") == 0);
    fixture_pkg_spec_t s4;
    memset(&s4, 0, sizeof(s4));
    s4.title_id = "CUSA91001";
    s4.title = "Http Four";
    s4.category = "gd";
    s4.version = "01.00";
    s4.with_icon = 1;
    assert(fixture_write_pkg(HT_ROOT "/PS4/HttpFour.pkg", 0, &s4) == 0);
    assert(fixture_grow_file(HT_ROOT "/PS4/HttpFour.pkg", 3 * 1024 * 1024 + 123) == 0);
    assert(fixture_write_ps5_pkg(HT_ROOT "/PS5/HttpFive.pkg", "PPSA91002", "Http Five", "gd",
                                 "01.000.000", 1) == 0);
    assert(fixture_write_ps5_pkg(HT_ROOT "/PS5/Deep/Deep Game.pkg", "PPSA91003", "Deep Game", "gd",
                                 "01.000.000", 0) == 0);
    FILE *f = fopen(HT_ROOT "/readme.txt", "w");
    assert(f);
    fputs("not a package\n", f);
    fclose(f);
}

static void test_url_parse(void) {
    http_url_t u;
    assert(http_url_parse("http://192.168.1.2:8080/pkgs/?x=1#frag", &u) == 0);
    assert(!u.https && u.port == 8080 && strcmp(u.host, "192.168.1.2") == 0);
    assert(strcmp(u.path, "/pkgs/?x=1") == 0);
    assert(http_url_parse("https://user:pw@nas.local/a", &u) == 0);
    assert(u.https && u.port == 443 && strcmp(u.host, "nas.local") == 0 && strcmp(u.path, "/a") == 0);
    assert(http_url_parse("http://[::1]:81", &u) == 0);
    assert(strcmp(u.host, "::1") == 0 && u.port == 81 && strcmp(u.path, "/") == 0);
    assert(http_url_parse("ftp://x/", &u) != 0);
    assert(http_url_parse("http://host:99999/", &u) != 0);

    http_source_config_t c;
    memset(&c, 0, sizeof(c));
    snprintf(c.url, sizeof(c.url), "  nas.local:8080/games/index.json ");
    assert(http_source_sanitize(&c) == 0);
    assert(strcmp(c.url, "http://nas.local:8080/games/") == 0);
    assert(strcmp(c.tls_mode, HTTP_TLS_VERIFY) == 0);
    assert(strncmp(c.id, "http_", 5) == 0 && c.label[0]);
    printf("  url parse / sanitize ok\n");
}

static void test_html_scan(void) {
    http_source_config_t c;
    make_config(&c, NULL, NULL);
    assert(http_source_sanitize(&c) == 0);
    found_t f;
    memset(&f, 0, sizeof(f));
    int n = http_source_scan(&c, collect_cb, &f);
    assert(n == 3 && f.count == 3);
    int i4 = find_name(&f, "HttpFour.pkg");
    int i5 = find_name(&f, "HttpFive.pkg");
    int id = find_name(&f, "Deep Game.pkg");
    assert(i4 >= 0 && i5 >= 0 && id >= 0);
    assert(f.sizes[i4] == file_size(HT_ROOT "/PS4/HttpFour.pkg"));
    assert(strstr(f.urls[id], "Deep%20Game.pkg") != NULL);
    assert(http_source_count_pkg_files(&c) == 3);

    http_source_test_result_t r;
    http_source_test(&c, &r);
    assert(r.success && r.pkg_count == 3 && r.range_supported == 1);
    assert(strcmp(r.listing, "html") == 0);
    printf("  html listing scan ok (%s)\n", r.message);
}

static void test_parse_and_icon(void) {
    char url[256];
    pkg_detail_t d;
    snprintf(url, sizeof(url), "%sPS4/HttpFour.pkg", g_base);
    assert(http_source_parse_pkg(url, &d) == 0);
    assert(strcmp(d.title_id, "CUSA91001") == 0);
    assert(strcmp(d.platform, "ps4") == 0);
    assert(d.file_size == file_size(HT_ROOT "/PS4/HttpFour.pkg"));
    assert(d.has_icon);
    uint8_t *icon = NULL;
    size_t isz = 0;
    assert(pkg_parser_get_icon(url, d.icon_offset, d.icon_size, &icon, &isz) == 0);
    assert(isz == FIXTURE_ICON_SIZE && memcmp(icon, kFixturePngMagic, 8) == 0);
    free(icon);

    snprintf(url, sizeof(url), "%sPS5/HttpFive.pkg", g_base);
    assert(pkg_parser_parse(url, &d) == 0); /* dispatch through pkg_parser */
    assert(strcmp(d.title_id, "PPSA91002") == 0 && strcmp(d.platform, "ps5") == 0);

    char sum1[64], sum2[64];
    assert(http_source_calc_checksum(url, sum1, sizeof(sum1)) == 0);
    assert(http_source_calc_checksum(url, sum2, sizeof(sum2)) == 0);
    assert(strcmp(sum1, sum2) == 0);

    snprintf(url, sizeof(url), "%sredir/PS5/HttpFive.pkg", g_base);
    uint64_t size = 0;
    assert(http_source_stat(url, &size, NULL) == 0);
    assert(size == file_size(HT_ROOT "/PS5/HttpFive.pkg"));
    printf("  parse / icon / checksum / redirect ok\n");
}

typedef struct {
    http_file_session_t *s;
    const uint8_t *ref;
    uint64_t size;
    int seed;
    int ok;
} reader_arg_t;

static void *reader_thread(void *arg) {
    reader_arg_t *a = (reader_arg_t *)arg;
    uint8_t *buf = malloc(300 * 1024);
    assert(buf);
    a->ok = 1;
    for (int i = 0; i < 40; i++) {
        uint64_t off = ((uint64_t)(a->seed * 7919 + i * 104729) * 97) % a->size;
        size_t want = 300 * 1024;
        if (off + want > a->size) want = (size_t)(a->size - off);
        ssize_t n = http_file_session_read(a->s, buf, want, off);
        if (n != (ssize_t)want || memcmp(buf, a->ref + off, want) != 0) a->ok = 0;
    }
    free(buf);
    return NULL;
}

static void test_session_streaming(void) {
    const char *path = HT_ROOT "/PS4/HttpFour.pkg";
    uint64_t size = file_size(path);
    uint8_t *ref = malloc(size);
    FILE *f = fopen(path, "rb");
    assert(ref && f && fread(ref, 1, size, f) == size);
    fclose(f);

    char url[256];
    snprintf(url, sizeof(url), "%sPS4/HttpFour.pkg", g_base);
    http_file_session_t *s = http_file_session_open(url);
    assert(s && http_file_session_get_size(s) == size);

    pthread_t t[4];
    reader_arg_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i].s = s;
        args[i].ref = ref;
        args[i].size = size;
        args[i].seed = i + 1;
        assert(pthread_create(&t[i], NULL, reader_thread, &args[i]) == 0);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(t[i], NULL);
        assert(args[i].ok);
    }
    uint8_t tail[64];
    assert(http_file_session_read(s, tail, sizeof(tail), size - 10) == 10);
    assert(memcmp(tail, ref + size - 10, 10) == 0);
    assert(http_file_session_read(s, tail, sizeof(tail), size) == 0);
    http_file_session_close(s);

    /* The virtual stream engine routes http:// the same way. */
    virtual_stream_t *vs = calloc(1, sizeof(*vs));
    assert(vs && virtual_stream_open(url, vs) == 0);
    assert(vs->is_http && vs->total_pkg_size == size);
    uint8_t mid[4096];
    assert(virtual_stream_read(vs, size / 2, mid, sizeof(mid)) == (ssize_t)sizeof(mid));
    assert(memcmp(mid, ref + size / 2, sizeof(mid)) == 0);
    virtual_stream_close(vs);
    free(vs);
    free(ref);
    printf("  pooled parallel range streaming ok\n");
}

static void test_index_json(void) {
    FILE *f = fopen(HT_ROOT "/index.json", "w");
    assert(f);
    fprintf(f, "{\"files\":[{\"path\":\"PS4/HttpFour.pkg\",\"size\":%llu,\"mtime\":1700000000},"
               "\"PS5/Deep/Deep Game.pkg\"]}",
            (unsigned long long)file_size(HT_ROOT "/PS4/HttpFour.pkg"));
    fclose(f);

    http_source_config_t c;
    make_config(&c, NULL, NULL);
    assert(http_source_sanitize(&c) == 0);
    found_t fnd;
    memset(&fnd, 0, sizeof(fnd));
    assert(http_source_scan(&c, collect_cb, &fnd) == 2);
    int id = find_name(&fnd, "Deep Game.pkg");
    assert(id >= 0 && fnd.sizes[id] == file_size(HT_ROOT "/PS5/Deep/Deep Game.pkg"));
    http_source_test_result_t r;
    http_source_test(&c, &r);
    assert(r.success && strcmp(r.listing, "index.json") == 0 && r.pkg_count == 2);
    unlink(HT_ROOT "/index.json");
    printf("  index.json discovery ok\n");
}

static void test_auth_and_no_range(void) {
    http_test_server_stop();
    http_test_server_opts_t o = { HT_ROOT, "alice", "s3cret", 0, 0 };
    assert(http_test_server_start(&o) == 0);
    snprintf(g_base, sizeof(g_base), "http://127.0.0.1:%d/", o.port);

    http_source_config_t c;
    make_config(&c, NULL, NULL);
    http_source_test_result_t r;
    http_source_test(&c, &r);
    assert(!r.success && strstr(r.message, "Access denied"));
    make_config(&c, "alice", "s3cret");
    http_source_test(&c, &r);
    assert(r.success && r.pkg_count == 3);

    /* Streaming a URL needs the stored source for credentials. */
    assert(http_source_sanitize(&c) == 0);
    assert(http_sources_set(&c, 1) == 1);
    char url[256];
    snprintf(url, sizeof(url), "%sPS5/HttpFive.pkg", g_base);
    http_file_session_t *s = http_file_session_open(url);
    assert(s);
    http_file_session_close(s);
    printf("  basic auth ok\n");

    http_test_server_stop();
    http_test_server_opts_t o2 = { HT_ROOT, NULL, NULL, 1, 0 };
    assert(http_test_server_start(&o2) == 0);
    snprintf(g_base, sizeof(g_base), "http://127.0.0.1:%d/", o2.port);
    make_config(&c, NULL, NULL);
    http_source_test(&c, &r);
    assert(!r.success && r.range_supported == 0 && strstr(r.message, "Range"));
    snprintf(url, sizeof(url), "%sPS5/HttpFive.pkg", g_base);
    assert(http_file_session_open(url) == NULL);
    printf("  no-range server rejected ok\n");

    http_test_server_stop();
    http_test_server_opts_t o3 = { HT_ROOT, NULL, NULL, 0, 0 };
    assert(http_test_server_start(&o3) == 0);
    snprintf(g_base, sizeof(g_base), "http://127.0.0.1:%d/", o3.port);
}

static void test_scanner_integration(void) {
    http_source_config_t c;
    make_config(&c, NULL, NULL);
    assert(http_source_sanitize(&c) == 0);
    assert(http_sources_set(&c, 1) == 1);

    pkg_scanner_init();
    int n = pkg_scanner_scan();
    assert(n >= 3);
    char *drives = pkg_scanner_drives_to_json();
    assert(drives && strstr(drives, "\"type\":\"http\""));
    free(drives);

    char *json = pkg_scanner_packages_for_drive_to_json(c.id);
    assert(json);
    assert(strstr(json, "\"title_id\":\"CUSA91001\""));
    assert(strstr(json, "\"platform\":\"ps4\",\"folder_platform\":\"ps4\",\"platform_mismatch\":false"));
    assert(strstr(json, "\"platform\":\"ps5\",\"folder_platform\":\"ps5\",\"platform_mismatch\":false"));
    free(json);

    int changed = 0;
    pkg_scanner_scan_quick(c.id, &changed);
    printf("  scanner integration ok (%d packages)\n", n);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("==============================================\n");
    printf(">>> RUNNING HTTP SOURCE TEST <<<\n");
    printf("==============================================\n");
    setup_tree();
    setenv("PKG_HTTP_SOURCES_PATH", HT_DIR "/http_sources.json", 1);
    setenv("PKG_CACHE_DIR", HT_DIR "/cache", 1);
    setenv("PKG_SETTINGS_PATH", HT_DIR "/settings.json", 1);
    setenv("PKG_USB_PREFIX", HT_DIR "/no_usb", 1);
    setenv("PKG_DISC_DIR", HT_DIR "/no_disc", 1);
    setenv("PKG_LOG_FILE", "none", 1);

    http_test_server_opts_t o = { HT_ROOT, NULL, NULL, 0, 0 };
    assert(http_test_server_start(&o) == 0);
    snprintf(g_base, sizeof(g_base), "http://127.0.0.1:%d/", o.port);

    test_url_parse();
    test_html_scan();
    test_parse_and_icon();
    test_session_streaming();
    test_index_json();
    test_auth_and_no_range();
    test_scanner_integration();

    http_test_server_stop();
    printf("\n>>> ALL HTTP SOURCE TESTS PASSED! <<<\n");
    return 0;
}
