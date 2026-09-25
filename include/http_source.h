#ifndef HTTP_SOURCE_H
#define HTTP_SOURCE_H

/*
 * PKG Manager X - HTTP/HTTPS package sources.
 *
 * A source is a base URL (e.g. http://nas.local:8080/pkgs/). Packages are
 * discovered from, in order:
 *   1. <base>/index.json  (see tools/make_http_index.py), or
 *   2. the server's HTML directory listing (nginx autoindex, Apache,
 *      python -m http.server, caddy file_server browse, ...), recursing
 *      into sub-folders such as PS4/ and PS5/.
 * Package bytes are read with HTTP Range requests, so the server must
 * answer "Range: bytes=a-b" with 206 Partial Content (all of the servers
 * above do). HTTPS uses mbedTLS when built with PKGMGR_HAVE_TLS.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "pkg_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HTTP_SOURCES 8
#define HTTP_URL_MAX 1024
#define HTTP_SOURCE_URL_MAX_LEN 250 /* base URL limit (fits pkg_drive_t.path) */

/* tls_mode values */
#define HTTP_TLS_VERIFY "verify" /* CA bundle (PKG_CA_BUNDLE or /data/pkgmgr/cacert.pem) */
#define HTTP_TLS_PIN    "pin"    /* SHA-256 fingerprint of the server certificate */
#define HTTP_TLS_NONE   "none"   /* no certificate check (LAN / self-signed) */

typedef struct {
    int enabled;
    char id[32];          /* "http_<suffix>" */
    char label[64];       /* "NAS (HTTP)" */
    char url[512];        /* base URL, always ends with '/' after sanitize */
    char username[64];    /* optional Basic auth */
    char password[128];
    char tls_mode[16];    /* HTTP_TLS_* (https only) */
    char tls_pin[100];    /* hex SHA-256, ':' separators allowed */
} http_source_config_t;

typedef struct {
    int https;
    char host[256];
    int port;
    char path[HTTP_URL_MAX]; /* starts with '/', includes query */
} http_url_t;

/* ── Configuration (persisted in PKG_HTTP_SOURCES_PATH or
 *    /data/pkgmgr/http_sources.json) ──────────────────────────────────── */
void http_sources_init(void);
int http_sources_get(http_source_config_t *out, int max_out);
int http_sources_set(const http_source_config_t *list, int count);
/* Parses {"sources":[...]} or a bare array / single object. Returns count. */
int http_sources_parse_json(const char *json, http_source_config_t *out, int max_out);
/* JSON array; passwords are never included (has_password flag instead). */
char *http_sources_to_json(void);
/* Normalizes url (scheme, trailing slash), label, id, tls_mode. Returns 0
 * when the config is usable, -1 otherwise. */
int http_source_sanitize(http_source_config_t *cfg);
/* Finds the configured source whose base URL prefixes url. */
int http_sources_find_for_url(const char *url, http_source_config_t *out);

/* ── URL helpers ──────────────────────────────────────────────────────── */
int http_url_parse(const char *url, http_url_t *out);
int http_source_is_https_supported(void);

/* ── Operations ───────────────────────────────────────────────────────── */
typedef struct {
    int success;
    int pkg_count;
    int range_supported;     /* -1 unknown (no packages to probe) */
    char listing[16];        /* "index.json" / "html" / "" */
    char fingerprint[100];   /* hex SHA-256 of server cert (https) */
    char message[512];
} http_source_test_result_t;

void http_source_test(const http_source_config_t *cfg, http_source_test_result_t *out);

typedef void (*http_pkg_callback_t)(const char *url, const char *filename,
                                    uint64_t file_size, uint32_t mtime,
                                    void *user_data);

/* Lists .pkg files. Returns count, or negative when the source is
 * unreachable (callers keep cached entries in that case). */
int http_source_scan(const http_source_config_t *cfg, http_pkg_callback_t cb, void *user_data);
int http_source_count_pkg_files(const http_source_config_t *cfg);

int http_source_stat(const char *url, uint64_t *out_size, uint32_t *out_mtime);
int http_source_calc_checksum(const char *url, char *out_checksum, size_t out_max);
int http_source_parse_pkg(const char *url, pkg_detail_t *out);
int http_source_get_icon(const char *url, uint64_t offset, uint32_t size,
                         uint8_t **out_data, size_t *out_size);
ssize_t http_source_pread(const char *url, void *buf, size_t count, uint64_t offset);

/* ── Streaming session ────────────────────────────────────────────────
 * Thread-safe: concurrent reads use separate pooled keep-alive
 * connections, so the installer's parallel range requests map to parallel
 * HTTP connections. */
typedef struct http_file_session http_file_session_t;

http_file_session_t *http_file_session_open(const char *url);
ssize_t http_file_session_read(http_file_session_t *s, void *buf, size_t count, uint64_t offset);
uint64_t http_file_session_get_size(http_file_session_t *s);
void http_file_session_close(http_file_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SOURCE_H */
