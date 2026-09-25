/*
 * PKG Manager X - HTTP/HTTPS Package Sources
 *
 * Minimal HTTP/1.1 client (keep-alive, Range, redirects, Basic auth,
 * chunked bodies) with optional mbedTLS, plus source configuration,
 * directory discovery (index.json / HTML autoindex) and a pooled
 * streaming session used by the virtual stream engine.
 */

#if !defined(_GNU_SOURCE) && !defined(PS4_BUILD) && !defined(PS5_BUILD)
#define _GNU_SOURCE /* strcasestr on glibc host test builds */
#endif
#include "http_source.h"
#include "pkg_parse_reader.h"
#include "pkg_cache.h"
#include "installer.h"
#include "version_x.h"
#include "miniz.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef PKGMGR_HAVE_TLS
#include <sys/sysctl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

#define HTTP_CONNECT_TIMEOUT_MS 5000
#define HTTP_IO_TIMEOUT_SEC     30
#define HTTP_RBUF_SIZE          (64 * 1024)
#define HTTP_MAX_REDIRECTS      5
#define HTTP_MAX_LISTING        (8 * 1024 * 1024)
#define HTTP_MAX_DEPTH          5
#define HTTP_MAX_DIRS           512
#define HTTP_POOL_MAX           6
#define HTTP_DEFAULT_CA_PATH    "/data/pkgmgr/cacert.pem"

#ifdef MSG_NOSIGNAL
#define HTTP_SEND_FLAGS MSG_NOSIGNAL
#else
#define HTTP_SEND_FLAGS 0
#endif

/* ══════════════════════════════════════════════════════════════════════
 * Small helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void copy_str(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    snprintf(dst, dst_sz, "%s", src ? src : "");
}

static void trim_inplace(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    size_t lead = 0;
    while (s[lead] && isspace((unsigned char)s[lead])) lead++;
    if (lead) memmove(s, s + lead, len - lead + 1);
}

static int ends_with_ci(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

static void b64_encode(const unsigned char *in, size_t n, char *out, size_t out_sz) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n && o + 5 < out_sz; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? tbl[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? tbl[v & 63] : '=';
    }
    out[o] = '\0';
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void pct_decode(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_sz; i++) {
        if (in[i] == '%' && hexval(in[i + 1]) >= 0 && hexval(in[i + 2]) >= 0) {
            out[o++] = (char)(hexval(in[i + 1]) * 16 + hexval(in[i + 2]));
            i += 2;
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

/* Percent-encodes everything except unreserved characters and '/'. */
static void pct_encode_path(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 4 < out_sz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

static void html_unescape_inplace(char *s) {
    static const struct { const char *ent; char ch; } ents[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''}, {"&#x27;", '\''}
    };
    char *w = s;
    for (char *r = s; *r;) {
        int matched = 0;
        if (*r == '&') {
            for (size_t k = 0; k < sizeof(ents) / sizeof(ents[0]); k++) {
                size_t l = strlen(ents[k].ent);
                if (strncasecmp(r, ents[k].ent, l) == 0) {
                    *w++ = ents[k].ch;
                    r += l;
                    matched = 1;
                    break;
                }
            }
        }
        if (!matched) *w++ = *r++;
    }
    *w = '\0';
}

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

/* RFC 7231 IMF-fixdate: "Sun, 06 Nov 1994 08:49:37 GMT". 0 on failure. */
static uint32_t parse_http_date(const char *s) {
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char *comma = strchr(s, ',');
    if (comma) s = comma + 1;
    int d = 0, y = 0, hh = 0, mm = 0, ss = 0;
    char mon[4] = {0};
    if (sscanf(s, " %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return 0;
    int m = 0;
    for (int i = 0; i < 12; i++) {
        if (strcasecmp(mon, months[i]) == 0) { m = i + 1; break; }
    }
    if (m == 0 || y < 1970) return 0;
    int64_t t = days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
    return t > 0 && t < 0xFFFFFFFFLL ? (uint32_t)t : 0;
}

static uint64_t fnv1a64(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

/* ══════════════════════════════════════════════════════════════════════
 * Minimal JSON helpers (flat objects, as used by our own files)
 * ══════════════════════════════════════════════════════════════════════ */

/* Pointer to the matching close bracket for *p ('{' or '['), or NULL. */
static const char *json_match(const char *p, const char *end) {
    char open = *p, close = (open == '{') ? '}' : ']';
    int depth = 0, in_str = 0, esc = 0;
    for (; p < end && *p; p++) {
        if (esc) { esc = 0; continue; }
        if (in_str) {
            if (*p == '\\') esc = 1;
            else if (*p == '"') in_str = 0;
            continue;
        }
        if (*p == '"') in_str = 1;
        else if (*p == open) depth++;
        else if (*p == close && --depth == 0) return p;
    }
    return NULL;
}

/* Parses a JSON string starting at p ('"'). Returns pointer after the
 * closing quote, or NULL. */
static const char *json_parse_string(const char *p, const char *end, char *out, size_t out_sz) {
    if (p >= end || *p != '"') return NULL;
    p++;
    size_t o = 0;
    while (p < end && *p && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char e = *p++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    unsigned v = 0;
                    for (int k = 0; k < 4 && p < end; k++) {
                        int h = hexval(*p++);
                        v = (v << 4) | (unsigned)(h < 0 ? 0 : h);
                    }
                    /* UTF-8 encode the BMP code point. */
                    if (v < 0x80) {
                        c = (char)v;
                    } else if (v < 0x800) {
                        if (o + 2 < out_sz) { out[o++] = (char)(0xC0 | (v >> 6)); }
                        c = (char)(0x80 | (v & 0x3F));
                    } else {
                        if (o + 3 < out_sz) {
                            out[o++] = (char)(0xE0 | (v >> 12));
                            out[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
                        }
                        c = (char)(0x80 | (v & 0x3F));
                    }
                    break;
                }
                default: c = e; break;
            }
        }
        if (o + 1 < out_sz) out[o++] = c;
    }
    if (out_sz) out[o] = '\0';
    return (p < end && *p == '"') ? p + 1 : NULL;
}

/* Finds "key": inside [obj, end) and returns the value start. */
static const char *json_value(const char *obj, const char *end, const char *key) {
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t pl = strlen(pat);
    for (const char *p = obj; p + pl <= end; p++) {
        if (*p != '"' || strncmp(p, pat, pl) != 0) continue;
        const char *q = p + pl;
        while (q < end && isspace((unsigned char)*q)) q++;
        if (q >= end || *q != ':') continue;
        q++;
        while (q < end && isspace((unsigned char)*q)) q++;
        return q < end ? q : NULL;
    }
    return NULL;
}

static int json_get_str(const char *obj, const char *end, const char *key, char *out, size_t out_sz) {
    const char *v = json_value(obj, end, key);
    if (!v || *v != '"') return -1;
    return json_parse_string(v, end, out, out_sz) ? 0 : -1;
}

static int json_get_u64(const char *obj, const char *end, const char *key, uint64_t *out) {
    const char *v = json_value(obj, end, key);
    if (!v) return -1;
    if (*v == '"') v++;
    if (!isdigit((unsigned char)*v)) return -1;
    *out = strtoull(v, NULL, 10);
    return 0;
}

static int json_get_bool(const char *obj, const char *end, const char *key, int *out) {
    const char *v = json_value(obj, end, key);
    if (!v) return -1;
    if (strncmp(v, "true", 4) == 0 || *v == '1') { *out = 1; return 0; }
    if (strncmp(v, "false", 5) == 0 || *v == '0') { *out = 0; return 0; }
    return -1;
}

static void json_escape(const char *src, char *dst, size_t dst_sz) {
    size_t d = 0;
    for (size_t s = 0; src && src[s] && d + 7 < dst_sz; s++) {
        unsigned char c = (unsigned char)src[s];
        if (c == '"' || c == '\\') { dst[d++] = '\\'; dst[d++] = (char)c; }
        else if (c == '\n') { dst[d++] = '\\'; dst[d++] = 'n'; }
        else if (c == '\r') { dst[d++] = '\\'; dst[d++] = 'r'; }
        else if (c == '\t') { dst[d++] = '\\'; dst[d++] = 't'; }
        else if (c < 32) { d += (size_t)snprintf(dst + d, dst_sz - d, "\\u%04x", c); }
        else dst[d++] = (char)c;
    }
    if (dst_sz) dst[d] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════
 * URL helpers
 * ══════════════════════════════════════════════════════════════════════ */

int http_url_parse(const char *url, http_url_t *out) {
    if (!url || !out) return -1;
    memset(out, 0, sizeof(*out));
    const char *p;
    if (strncasecmp(url, "https://", 8) == 0) {
        out->https = 1;
        out->port = 443;
        p = url + 8;
    } else if (strncasecmp(url, "http://", 7) == 0) {
        out->port = 80;
        p = url + 7;
    } else {
        return -1;
    }

    const char *auth_end = p + strcspn(p, "/?#");
    for (const char *q = p; q < auth_end; q++) {
        if (*q == '@') p = q + 1; /* strip userinfo; credentials live in the config */
    }

    const char *host_start = p, *host_end, *port_str = NULL;
    if (*p == '[') {
        const char *rb = memchr(p, ']', (size_t)(auth_end - p));
        if (!rb) return -1;
        host_start = p + 1;
        host_end = rb;
        if (rb + 1 < auth_end && rb[1] == ':') port_str = rb + 2;
    } else {
        const char *colon = memchr(p, ':', (size_t)(auth_end - p));
        host_end = colon ? colon : auth_end;
        if (colon) port_str = colon + 1;
    }
    size_t hl = (size_t)(host_end - host_start);
    if (hl == 0 || hl >= sizeof(out->host)) return -1;
    memcpy(out->host, host_start, hl);
    out->host[hl] = '\0';

    if (port_str) {
        if (port_str >= auth_end) return -1;
        int port = 0;
        for (const char *q = port_str; q < auth_end; q++) {
            if (!isdigit((unsigned char)*q)) return -1;
            port = port * 10 + (*q - '0');
            if (port > 65535) return -1;
        }
        if (port == 0) return -1;
        out->port = port;
    }

    const char *path = auth_end;
    size_t plen = strcspn(path, "#");
    if (plen == 0) {
        strcpy(out->path, "/");
    } else if (*path == '?') {
        if (plen + 2 > sizeof(out->path)) return -1;
        out->path[0] = '/';
        memcpy(out->path + 1, path, plen);
        out->path[plen + 1] = '\0';
    } else {
        if (plen + 1 > sizeof(out->path)) return -1;
        memcpy(out->path, path, plen);
        out->path[plen] = '\0';
    }
    return 0;
}

static void url_origin(const http_url_t *u, char *out, size_t out_sz) {
    int def = u->https ? 443 : 80;
    int v6 = strchr(u->host, ':') != NULL;
    if (u->port != def) {
        snprintf(out, out_sz, "%s://%s%s%s:%d", u->https ? "https" : "http",
                 v6 ? "[" : "", u->host, v6 ? "]" : "", u->port);
    } else {
        snprintf(out, out_sz, "%s://%s%s%s", u->https ? "https" : "http",
                 v6 ? "[" : "", u->host, v6 ? "]" : "");
    }
}

/* Resolves href against base (a URL). Drops fragments. */
static int url_resolve(const char *base, const char *href, char *out, size_t out_sz) {
    char tmp[HTTP_URL_MAX * 2];
    if (strncasecmp(href, "http://", 7) == 0 || strncasecmp(href, "https://", 8) == 0) {
        snprintf(tmp, sizeof(tmp), "%s", href);
    } else if (href[0] == '/' && href[1] == '/') {
        snprintf(tmp, sizeof(tmp), "%s:%s", strncasecmp(base, "https", 5) == 0 ? "https" : "http", href);
    } else {
        http_url_t bu;
        if (http_url_parse(base, &bu) != 0) return -1;
        char origin[320];
        url_origin(&bu, origin, sizeof(origin));
        if (href[0] == '/') {
            snprintf(tmp, sizeof(tmp), "%s%s", origin, href);
        } else {
            char dir[HTTP_URL_MAX];
            snprintf(dir, sizeof(dir), "%s", bu.path);
            char *q = strchr(dir, '?');
            if (q) *q = '\0';
            char *slash = strrchr(dir, '/');
            if (slash) slash[1] = '\0';
            const char *h = href;
            while (strncmp(h, "./", 2) == 0) h += 2;
            snprintf(tmp, sizeof(tmp), "%s%s%s", origin, dir, h);
        }
    }
    char *frag = strchr(tmp, '#');
    if (frag) *frag = '\0';
    if (strlen(tmp) >= out_sz) return -1;
    strcpy(out, tmp);
    return 0;
}

static int same_endpoint(const http_url_t *a, const http_url_t *b) {
    return a->https == b->https && a->port == b->port && strcasecmp(a->host, b->host) == 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * Source configuration store
 * ══════════════════════════════════════════════════════════════════════ */

static http_source_config_t g_sources[MAX_HTTP_SOURCES];
static int g_source_count = 0;
static int g_sources_loaded = 0;
static pthread_mutex_t g_src_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *sources_file_path(void) {
    const char *env = getenv("PKG_HTTP_SOURCES_PATH");
    if (env && env[0] != '\0') return env;
    struct stat st;
    if (stat("/data", &st) == 0 && S_ISDIR(st.st_mode)) {
        mkdir("/data/pkgmgr", 0777);
        return "/data/pkgmgr/http_sources.json";
    }
    return "/tmp/pkgmgr_http_sources.json";
}

int http_source_sanitize(http_source_config_t *c) {
    if (!c) return -1;
    trim_inplace(c->url);
    trim_inplace(c->label);
    trim_inplace(c->username);
    trim_inplace(c->tls_pin);
    if (c->url[0] == '\0') return -1;

    if (strncasecmp(c->url, "http://", 7) != 0 && strncasecmp(c->url, "https://", 8) != 0) {
        char tmp[sizeof(c->url)];
        snprintf(tmp, sizeof(tmp), "http://%s", c->url);
        copy_str(c->url, sizeof(c->url), tmp);
    }
    /* Lower-case the scheme so prefix matches are stable. */
    for (char *p = c->url; *p && *p != ':'; p++) *p = (char)tolower((unsigned char)*p);

    /* A pasted ".../index.json" means its folder. */
    if (ends_with_ci(c->url, "/index.json")) c->url[strlen(c->url) - strlen("index.json")] = '\0';
    char *q = strpbrk(c->url, "?#");
    if (q) *q = '\0';
    size_t len = strlen(c->url);
    if (len > 0 && c->url[len - 1] != '/' && len + 1 < sizeof(c->url)) {
        c->url[len] = '/';
        c->url[len + 1] = '\0';
    }

    http_url_t u;
    if (http_url_parse(c->url, &u) != 0) return -1;
    /* The URL doubles as the scanner's drive path (pkg_drive_t.path[256]). */
    if (strlen(c->url) >= HTTP_SOURCE_URL_MAX_LEN) return -1;

    if (strcmp(c->tls_mode, HTTP_TLS_PIN) != 0 && strcmp(c->tls_mode, HTTP_TLS_NONE) != 0) {
        copy_str(c->tls_mode, sizeof(c->tls_mode), HTTP_TLS_VERIFY);
    }
    if (c->label[0] == '\0') {
        snprintf(c->label, sizeof(c->label), "%s%.40s", u.host, strcmp(u.path, "/") ? u.path : "");
    }
    if (c->id[0] == '\0') {
        snprintf(c->id, sizeof(c->id), "http_%08x",
                 (unsigned)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)c->url, strlen(c->url)));
    }
    return 0;
}

int http_sources_parse_json(const char *json, http_source_config_t *out, int max_out) {
    if (!json || !out || max_out <= 0) return 0;
    const char *end = json + strlen(json);
    const char *p = json;
    const char *arr = NULL;

    const char *key = json_value(json, end, "sources");
    if (key && *key == '[') arr = key;
    while (!arr && p < end && isspace((unsigned char)*p)) p++;
    if (!arr && p < end && *p == '[') arr = p;

    int count = 0;
    if (!arr) {
        /* Single object body (test endpoint). */
        if (p < end && *p == '{') {
            const char *oe = json_match(p, end);
            if (!oe) return 0;
            http_source_config_t *c = &out[0];
            memset(c, 0, sizeof(*c));
            c->enabled = 1;
            json_get_str(p, oe, "id", c->id, sizeof(c->id));
            json_get_str(p, oe, "label", c->label, sizeof(c->label));
            json_get_str(p, oe, "url", c->url, sizeof(c->url));
            json_get_str(p, oe, "username", c->username, sizeof(c->username));
            json_get_str(p, oe, "password", c->password, sizeof(c->password));
            json_get_str(p, oe, "tls_mode", c->tls_mode, sizeof(c->tls_mode));
            json_get_str(p, oe, "tls_pin", c->tls_pin, sizeof(c->tls_pin));
            json_get_bool(p, oe, "enabled", &c->enabled);
            return c->url[0] ? 1 : 0;
        }
        return 0;
    }

    const char *arr_end = json_match(arr, end);
    if (!arr_end) return 0;
    p = arr + 1;
    while (p < arr_end && count < max_out) {
        while (p < arr_end && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (p >= arr_end || *p != '{') break;
        const char *oe = json_match(p, arr_end + 1);
        if (!oe) break;
        http_source_config_t *c = &out[count];
        memset(c, 0, sizeof(*c));
        c->enabled = 1;
        json_get_str(p, oe, "id", c->id, sizeof(c->id));
        json_get_str(p, oe, "label", c->label, sizeof(c->label));
        json_get_str(p, oe, "url", c->url, sizeof(c->url));
        json_get_str(p, oe, "username", c->username, sizeof(c->username));
        json_get_str(p, oe, "password", c->password, sizeof(c->password));
        json_get_str(p, oe, "tls_mode", c->tls_mode, sizeof(c->tls_mode));
        json_get_str(p, oe, "tls_pin", c->tls_pin, sizeof(c->tls_pin));
        json_get_bool(p, oe, "enabled", &c->enabled);
        if (c->url[0] != '\0') count++;
        p = oe + 1;
    }
    return count;
}

static void sources_save_locked(void) {
    const char *path = sources_file_path();
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE *f = fopen(tmp_path, "w");
    if (!f) return;
    fprintf(f, "{\n  \"version\": 1,\n  \"sources\": [\n");
    for (int i = 0; i < g_source_count; i++) {
        const http_source_config_t *c = &g_sources[i];
        char e_id[80], e_label[160], e_url[1100], e_user[160], e_pass[300], e_mode[40], e_pin[220];
        json_escape(c->id, e_id, sizeof(e_id));
        json_escape(c->label, e_label, sizeof(e_label));
        json_escape(c->url, e_url, sizeof(e_url));
        json_escape(c->username, e_user, sizeof(e_user));
        json_escape(c->password, e_pass, sizeof(e_pass));
        json_escape(c->tls_mode, e_mode, sizeof(e_mode));
        json_escape(c->tls_pin, e_pin, sizeof(e_pin));
        fprintf(f, "    {\"id\": \"%s\", \"label\": \"%s\", \"url\": \"%s\", \"username\": \"%s\", "
                   "\"password\": \"%s\", \"tls_mode\": \"%s\", \"tls_pin\": \"%s\", \"enabled\": %s}%s\n",
                e_id, e_label, e_url, e_user, e_pass, e_mode, e_pin,
                c->enabled ? "true" : "false", (i + 1 < g_source_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    if (fflush(f) != 0) {
        fclose(f);
        unlink(tmp_path);
        return;
    }
    fclose(f);
    rename(tmp_path, path);
}

static void sources_load_locked(void) {
    g_source_count = 0;
    g_sources_loaded = 1;
    FILE *f = fopen(sources_file_path(), "r");
    if (!f) return;
    char *buf = (char *)malloc(65536);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t rd = fread(buf, 1, 65535, f);
    fclose(f);
    buf[rd] = '\0';
    http_source_config_t tmp[MAX_HTTP_SOURCES];
    int n = http_sources_parse_json(buf, tmp, MAX_HTTP_SOURCES);
    free(buf);
    for (int i = 0; i < n; i++) {
        if (http_source_sanitize(&tmp[i]) == 0) g_sources[g_source_count++] = tmp[i];
    }
}

void http_sources_init(void) {
    pthread_mutex_lock(&g_src_mutex);
    sources_load_locked();
    pthread_mutex_unlock(&g_src_mutex);
}

int http_sources_get(http_source_config_t *out, int max_out) {
    pthread_mutex_lock(&g_src_mutex);
    if (!g_sources_loaded) sources_load_locked();
    int n = g_source_count < max_out ? g_source_count : max_out;
    if (out && n > 0) memcpy(out, g_sources, (size_t)n * sizeof(*out));
    pthread_mutex_unlock(&g_src_mutex);
    return n;
}

int http_sources_set(const http_source_config_t *list, int count) {
    if (count < 0) count = 0;
    if (count > MAX_HTTP_SOURCES) count = MAX_HTTP_SOURCES;
    pthread_mutex_lock(&g_src_mutex);
    g_source_count = 0;
    for (int i = 0; i < count; i++) {
        http_source_config_t c = list[i];
        if (http_source_sanitize(&c) == 0) g_sources[g_source_count++] = c;
    }
    g_sources_loaded = 1;
    sources_save_locked();
    int n = g_source_count;
    pthread_mutex_unlock(&g_src_mutex);
    return n;
}

char *http_sources_to_json(void) {
    http_source_config_t list[MAX_HTTP_SOURCES];
    int n = http_sources_get(list, MAX_HTTP_SOURCES);
    size_t cap = 256 + (size_t)n * 2048;
    char *json = (char *)malloc(cap);
    if (!json) return NULL;
    size_t pos = (size_t)snprintf(json, cap, "[");
    for (int i = 0; i < n; i++) {
        const http_source_config_t *c = &list[i];
        char e_id[80], e_label[160], e_url[1100], e_user[160], e_mode[40], e_pin[220];
        json_escape(c->id, e_id, sizeof(e_id));
        json_escape(c->label, e_label, sizeof(e_label));
        json_escape(c->url, e_url, sizeof(e_url));
        json_escape(c->username, e_user, sizeof(e_user));
        json_escape(c->tls_mode, e_mode, sizeof(e_mode));
        json_escape(c->tls_pin, e_pin, sizeof(e_pin));
        int w = snprintf(json + pos, cap - pos,
                         "%s{\"id\":\"%s\",\"label\":\"%s\",\"url\":\"%s\",\"username\":\"%s\","
                         "\"password\":\"\",\"has_password\":%s,\"tls_mode\":\"%s\",\"tls_pin\":\"%s\","
                         "\"enabled\":%s}",
                         i ? "," : "", e_id, e_label, e_url, e_user,
                         c->password[0] ? "true" : "false", e_mode, e_pin,
                         c->enabled ? "true" : "false");
        if (w < 0 || (size_t)w >= cap - pos) break;
        pos += (size_t)w;
    }
    snprintf(json + pos, cap - pos, "]");
    return json;
}

int http_sources_find_for_url(const char *url, http_source_config_t *out) {
    if (!url) return -1;
    http_source_config_t list[MAX_HTTP_SOURCES];
    int n = http_sources_get(list, MAX_HTTP_SOURCES);
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(list[i].url);
        if (l > best_len && strncasecmp(url, list[i].url, l) == 0) {
            best = i;
            best_len = l;
        }
    }
    if (best < 0) return -1;
    if (out) *out = list[best];
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * TLS (mbedTLS)
 * ══════════════════════════════════════════════════════════════════════ */

int http_source_is_https_supported(void) {
#ifdef PKGMGR_HAVE_TLS
    return 1;
#else
    return 0;
#endif
}

#ifdef PKGMGR_HAVE_TLS

/* Entropy: FreeBSD kern.arandom sysctl (available to payloads on PS4 and
 * PS5), with /dev/urandom as fallback. Also exported as the hardware poll
 * hook for mbedTLS builds with MBEDTLS_ENTROPY_HARDWARE_ALT. */
static int pkgmgr_entropy_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    size_t done = 0;
#if defined(CTL_KERN) && defined(KERN_ARND)
    while (done < len) {
        int mib[2] = {CTL_KERN, KERN_ARND};
        size_t want = len - done;
        if (want > 256) want = 256;
        size_t got = want;
        if (sysctl(mib, 2, output + done, &got, NULL, 0) != 0 || got == 0) break;
        done += got;
    }
#endif
    if (done < len) {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) fd = open("/dev/random", O_RDONLY);
        if (fd >= 0) {
            while (done < len) {
                ssize_t n = read(fd, output + done, len - done);
                if (n <= 0) break;
                done += (size_t)n;
            }
            close(fd);
        }
    }
    *olen = done;
    return done == len ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    return pkgmgr_entropy_poll(data, output, len, olen);
}

/* MBEDTLS_PLATFORM_ZEROIZE_ALT (see build_deps.sh): payload libcs do not
 * reliably export explicit_bzero, so wipe through a volatile pointer. */
void mbedtls_platform_zeroize(void *buf, size_t len);
void mbedtls_platform_zeroize(void *buf, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)buf;
    while (len--) *p++ = 0;
}

static pthread_mutex_t g_tls_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_tls_ready = 0;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_x509_crt g_ca;
static int g_ca_loaded = 0;

static int tls_global_init(void) {
    pthread_mutex_lock(&g_tls_mutex);
    if (!g_tls_ready) {
        mbedtls_entropy_init(&g_entropy);
        mbedtls_entropy_add_source(&g_entropy, pkgmgr_entropy_poll, NULL, 32,
                                   MBEDTLS_ENTROPY_SOURCE_STRONG);
        mbedtls_ctr_drbg_init(&g_drbg);
        static const char pers[] = "pkg-manager-x";
        if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                                  (const unsigned char *)pers, sizeof(pers) - 1) == 0) {
            g_tls_ready = 1;
        } else {
            mbedtls_ctr_drbg_free(&g_drbg);
            mbedtls_entropy_free(&g_entropy);
        }
    }
    int ok = g_tls_ready;
    pthread_mutex_unlock(&g_tls_mutex);
    return ok ? 0 : -1;
}

static int tls_rng(void *ctx, unsigned char *out, size_t len) {
    (void)ctx;
    pthread_mutex_lock(&g_tls_mutex);
    int r = mbedtls_ctr_drbg_random(&g_drbg, out, len);
    pthread_mutex_unlock(&g_tls_mutex);
    return r;
}

static const char *ca_bundle_path(void) {
    const char *env = getenv("PKG_CA_BUNDLE");
    return (env && env[0]) ? env : HTTP_DEFAULT_CA_PATH;
}

/* Loads the CA bundle once; retries later if it was missing/invalid. */
static int tls_load_ca(char *err, size_t err_sz) {
    pthread_mutex_lock(&g_tls_mutex);
    if (g_ca_loaded != 1) {
        const char *path = ca_bundle_path();
        struct stat st;
        if (stat(path, &st) != 0) {
            pthread_mutex_unlock(&g_tls_mutex);
            snprintf(err, err_sz, "No CA bundle at %s. Copy a cacert.pem there, or trust the "
                                  "server certificate fingerprint instead", path);
            return -1;
        }
        if (g_ca_loaded == -1) mbedtls_x509_crt_free(&g_ca);
        mbedtls_x509_crt_init(&g_ca);
        int r = mbedtls_x509_crt_parse_file(&g_ca, path);
        g_ca_loaded = (r >= 0 && g_ca.version != 0) ? 1 : -1;
        if (g_ca_loaded != 1) {
            pthread_mutex_unlock(&g_tls_mutex);
            snprintf(err, err_sz, "CA bundle %s could not be parsed (-0x%04x)", path, (unsigned)-r);
            return -1;
        }
    }
    pthread_mutex_unlock(&g_tls_mutex);
    return 0;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n = send(fd, buf, len, HTTP_SEND_FLAGS);
    if (n >= 0) return (int)n;
    if (errno == EINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return -0x004E; /* MBEDTLS_ERR_NET_SEND_FAILED */
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n = recv(fd, buf, len, 0);
    if (n >= 0) return (int)n;
    if (errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_TIMEOUT;
    return -0x004C; /* MBEDTLS_ERR_NET_RECV_FAILED */
}

static void normalize_hex(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    for (; in && *in && o + 1 < out_sz; in++) {
        if (isxdigit((unsigned char)*in)) out[o++] = (char)tolower((unsigned char)*in);
    }
    out[o] = '\0';
}

#endif /* PKGMGR_HAVE_TLS */

/* ══════════════════════════════════════════════════════════════════════
 * Connections
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    int fd;
    int alive;
    http_url_t ep;           /* endpoint (host/port/https) this conn talks to */
    char fingerprint[100];   /* hex SHA-256 of the server cert (https) */
    size_t rpos, rlen;
    unsigned char rbuf[HTTP_RBUF_SIZE];
#ifdef PKGMGR_HAVE_TLS
    int tls_active;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
#endif
} http_conn_t;

static void conn_close(http_conn_t *c) {
    if (!c) return;
#ifdef PKGMGR_HAVE_TLS
    if (c->tls_active) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
        c->tls_active = 0;
    }
#endif
    if (c->fd >= 0) close(c->fd);
    free(c);
}

static int tcp_connect(const char *host, int port, char *err, size_t err_sz) {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0 || !res) {
        snprintf(err, err_sz, "Cannot resolve host '%s'", host);
        return -1;
    }

    int fd = -1;
    int last_errno = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) { last_errno = errno; continue; }
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            rc = poll(&pfd, 1, HTTP_CONNECT_TIMEOUT_MS);
            if (rc == 1) {
                int so_err = 0;
                socklen_t sl = sizeof(so_err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &sl);
                rc = so_err ? -1 : 0;
                if (so_err) errno = so_err;
            } else {
                if (rc == 0) errno = ETIMEDOUT;
                rc = -1;
            }
        }
        if (rc == 0) {
            fcntl(fd, F_SETFL, flags);
            break;
        }
        last_errno = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        snprintf(err, err_sz, "Cannot connect to %s:%d (%s)", host, port, strerror(last_errno));
        return -1;
    }

    struct timeval tv = { HTTP_IO_TIMEOUT_SEC, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int rcvbuf = 2 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    return fd;
}

#ifdef PKGMGR_HAVE_TLS
static int tls_handshake(http_conn_t *c, const http_source_config_t *cfg, const char *force_mode,
                         char *err, size_t err_sz) {
    if (tls_global_init() != 0) {
        snprintf(err, err_sz, "TLS random generator could not be seeded");
        return -1;
    }
    const char *mode = force_mode ? force_mode : (cfg ? cfg->tls_mode : HTTP_TLS_VERIFY);
    if (!mode || !mode[0]) mode = HTTP_TLS_VERIFY;
    if (strcmp(mode, HTTP_TLS_VERIFY) == 0 && tls_load_ca(err, err_sz) != 0) return -1;

    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    c->tls_active = 1;
    int r = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) {
        snprintf(err, err_sz, "TLS config failed (-0x%04x)", (unsigned)-r);
        return -1;
    }
    mbedtls_ssl_conf_rng(&c->conf, tls_rng, NULL);
    if (strcmp(mode, HTTP_TLS_VERIFY) == 0) {
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&c->conf, &g_ca, NULL);
    } else {
        mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    if ((r = mbedtls_ssl_setup(&c->ssl, &c->conf)) != 0 ||
        (r = mbedtls_ssl_set_hostname(&c->ssl, c->ep.host)) != 0) {
        snprintf(err, err_sz, "TLS setup failed (-0x%04x)", (unsigned)-r);
        return -1;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, bio_send, bio_recv, NULL);

    while ((r = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        char ebuf[160];
        mbedtls_strerror(r, ebuf, sizeof(ebuf));
        uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);
        if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != 0 && flags != (uint32_t)-1) {
            char vbuf[256];
            mbedtls_x509_crt_verify_info(vbuf, sizeof(vbuf), "", flags);
            trim_inplace(vbuf);
            snprintf(err, err_sz, "Certificate not trusted: %s", vbuf);
        } else {
            snprintf(err, err_sz, "TLS handshake failed: %s", ebuf);
        }
        return -1;
    }

    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&c->ssl);
    if (peer) {
        unsigned char hash[32];
        mbedtls_sha256(peer->raw.p, peer->raw.len, hash, 0);
        for (int i = 0; i < 32; i++) snprintf(c->fingerprint + i * 2, 3, "%02x", hash[i]);
    }
    if (strcmp(mode, HTTP_TLS_PIN) == 0) {
        char want[100];
        normalize_hex(cfg ? cfg->tls_pin : "", want, sizeof(want));
        if (want[0] == '\0' || strcmp(want, c->fingerprint) != 0) {
            snprintf(err, err_sz, "Server certificate fingerprint does not match the trusted one");
            return -1;
        }
    }
    return 0;
}
#endif

static http_conn_t *conn_open(const http_url_t *u, const http_source_config_t *cfg,
                              const char *force_tls_mode, char *err, size_t err_sz) {
    (void)cfg;
    (void)force_tls_mode;
#ifndef PKGMGR_HAVE_TLS
    if (u->https) {
        snprintf(err, err_sz, "HTTPS is not supported by this build");
        return NULL;
    }
#endif
    http_conn_t *c = (http_conn_t *)calloc(1, sizeof(http_conn_t));
    if (!c) {
        snprintf(err, err_sz, "Out of memory");
        return NULL;
    }
    c->fd = -1;
    c->ep = *u;
    c->ep.path[0] = '\0';
    c->fd = tcp_connect(u->host, u->port, err, err_sz);
    if (c->fd < 0) {
        free(c);
        return NULL;
    }
#ifdef PKGMGR_HAVE_TLS
    if (u->https && tls_handshake(c, cfg, force_tls_mode, err, err_sz) != 0) {
        conn_close(c);
        return NULL;
    }
#endif
    c->alive = 1;
    return c;
}

static int conn_write_all(http_conn_t *c, const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    while (len > 0) {
        ssize_t n;
#ifdef PKGMGR_HAVE_TLS
        if (c->tls_active) {
            int r = mbedtls_ssl_write(&c->ssl, p, len);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            n = r;
        } else
#endif
        {
            n = send(c->fd, p, len, HTTP_SEND_FLAGS);
            if (n < 0 && errno == EINTR) continue;
        }
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static ssize_t conn_read_raw(http_conn_t *c, void *buf, size_t len) {
    for (;;) {
#ifdef PKGMGR_HAVE_TLS
        if (c->tls_active) {
            int r = mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, len);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
            return r;
        }
#endif
        ssize_t n = recv(c->fd, buf, len, 0);
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

static int conn_getc(http_conn_t *c) {
    if (c->rpos >= c->rlen) {
        ssize_t n = conn_read_raw(c, c->rbuf, sizeof(c->rbuf));
        if (n <= 0) return -1;
        c->rpos = 0;
        c->rlen = (size_t)n;
    }
    return c->rbuf[c->rpos++];
}

/* Reads one CRLF/LF-terminated line (without the terminator). Returns
 * its length, or -1 on EOF/error before any byte. */
static int conn_read_line(http_conn_t *c, char *line, size_t max) {
    size_t n = 0;
    int got_any = 0;
    for (;;) {
        int ch = conn_getc(c);
        if (ch < 0) return got_any ? (int)n : -1;
        got_any = 1;
        if (ch == '\n') break;
        if (ch != '\r' && n + 1 < max) line[n++] = (char)ch;
    }
    line[n] = '\0';
    return (int)n;
}

static int conn_read_exact(http_conn_t *c, void *buf, size_t len) {
    unsigned char *p = (unsigned char *)buf;
    size_t avail = c->rlen - c->rpos;
    if (avail > 0) {
        size_t take = avail < len ? avail : len;
        memcpy(p, c->rbuf + c->rpos, take);
        c->rpos += take;
        p += take;
        len -= take;
    }
    while (len > 0) {
        ssize_t n = conn_read_raw(c, p, len);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * Requests / responses
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    int status;
    int64_t content_length;  /* -1 when unknown */
    int chunked;
    int conn_close;
    int has_range;
    uint64_t range_start, range_end, range_total;
    uint32_t last_modified;
    char location[HTTP_URL_MAX];
    char content_type[96];
    size_t body_read;        /* bytes delivered into a range buffer */
} http_resp_t;

static int cfg_applies_to(const http_source_config_t *cfg, const http_url_t *u) {
    if (!cfg || cfg->username[0] == '\0') return 0;
    http_url_t cu;
    return http_url_parse(cfg->url, &cu) == 0 && same_endpoint(&cu, u);
}

static int http_send_request(http_conn_t *c, const http_url_t *u, const http_source_config_t *cfg,
                             const char *method, int has_range, uint64_t rs, uint64_t re) {
    char host_hdr[300];
    int v6 = strchr(u->host, ':') != NULL;
    int def = u->https ? 443 : 80;
    if (u->port != def) {
        snprintf(host_hdr, sizeof(host_hdr), "%s%s%s:%d", v6 ? "[" : "", u->host, v6 ? "]" : "", u->port);
    } else {
        snprintf(host_hdr, sizeof(host_hdr), "%s%s%s", v6 ? "[" : "", u->host, v6 ? "]" : "");
    }

    char auth[400] = "";
    if (cfg_applies_to(cfg, u)) {
        char creds[200], b64[300];
        snprintf(creds, sizeof(creds), "%s:%s", cfg->username, cfg->password);
        b64_encode((const unsigned char *)creds, strlen(creds), b64, sizeof(b64));
        snprintf(auth, sizeof(auth), "Authorization: Basic %s\r\n", b64);
    }

    char range[96] = "";
    if (has_range) {
        snprintf(range, sizeof(range), "Range: bytes=%llu-%llu\r\n",
                 (unsigned long long)rs, (unsigned long long)re);
    }

    char req[HTTP_URL_MAX + 1024];
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: pkg-manager-x/%s\r\n"
                     "Accept: */*\r\n"
                     "Accept-Encoding: identity\r\n"
                     "Connection: keep-alive\r\n"
                     "%s%s\r\n",
                     method, u->path, host_hdr, PKGMGR_X_VERSION, range, auth);
    if (n < 0 || (size_t)n >= sizeof(req)) return -1;
    return conn_write_all(c, req, (size_t)n);
}

static int http_read_response(http_conn_t *c, http_resp_t *r) {
    memset(r, 0, sizeof(*r));
    r->content_length = -1;
    char line[4096];
    int n;
    /* Skip interim 1xx responses. */
    for (;;) {
        n = conn_read_line(c, line, sizeof(line));
        if (n <= 0) return -1;
        if (strncmp(line, "HTTP/1.", 7) != 0 || strlen(line) < 12) return -1;
        r->status = atoi(line + 9);
        r->conn_close = (line[7] == '0'); /* HTTP/1.0 closes by default */
        if (r->status >= 200) break;
        while ((n = conn_read_line(c, line, sizeof(line))) > 0) {}
        if (n < 0) return -1;
    }

    for (int count = 0; count < 128; count++) {
        n = conn_read_line(c, line, sizeof(line));
        if (n < 0) return -1;
        if (n == 0) return 0;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        trim_inplace(val);

        if (strcasecmp(line, "Content-Length") == 0) {
            r->content_length = (int64_t)strtoll(val, NULL, 10);
        } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
            if (strcasestr(val, "chunked")) r->chunked = 1;
        } else if (strcasecmp(line, "Connection") == 0) {
            if (strcasecmp(val, "close") == 0) r->conn_close = 1;
            else if (strcasecmp(val, "keep-alive") == 0) r->conn_close = 0;
        } else if (strcasecmp(line, "Location") == 0) {
            copy_str(r->location, sizeof(r->location), val);
        } else if (strcasecmp(line, "Content-Type") == 0) {
            copy_str(r->content_type, sizeof(r->content_type), val);
        } else if (strcasecmp(line, "Last-Modified") == 0) {
            r->last_modified = parse_http_date(val);
        } else if (strcasecmp(line, "Content-Range") == 0) {
            unsigned long long a = 0, b = 0, t = 0;
            if (sscanf(val, "bytes %llu-%llu/%llu", &a, &b, &t) == 3) {
                r->has_range = 1;
                r->range_start = a;
                r->range_end = b;
                r->range_total = t;
            } else if (sscanf(val, "bytes */%llu", &t) == 1) {
                r->range_total = t;
            }
        }
    }
    return -1;
}

/* Reads the whole body. out == NULL discards up to max bytes; beyond that
 * the connection is marked dead instead of draining. */
static int http_read_body(http_conn_t *c, http_resp_t *r, int is_head,
                          char **out, size_t *out_len, size_t max) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (is_head || r->status == 204 || r->status == 304) return 0;

    size_t cap = 0, len = 0;
    char *buf = NULL;
    char tmp[16384];

#define BODY_APPEND(src, n) do {                                              \
        if (out) {                                                            \
            if (len + (n) + 1 > cap) {                                        \
                size_t ncap = cap ? cap * 2 : 65536;                          \
                while (ncap < len + (n) + 1) ncap *= 2;                       \
                char *nb = (char *)realloc(buf, ncap);                        \
                if (!nb) { free(buf); return -1; }                            \
                buf = nb;                                                     \
                cap = ncap;                                                   \
            }                                                                 \
            memcpy(buf + len, (src), (n));                                    \
        }                                                                     \
        len += (n);                                                           \
    } while (0)

    if (r->chunked) {
        char line[128];
        for (;;) {
            if (conn_read_line(c, line, sizeof(line)) < 0) goto fail;
            unsigned long long chunk = strtoull(line, NULL, 16);
            if (chunk == 0) {
                while (conn_read_line(c, line, sizeof(line)) > 0) {}
                break;
            }
            if (len + chunk > max) goto too_big;
            while (chunk > 0) {
                size_t take = chunk > sizeof(tmp) ? sizeof(tmp) : (size_t)chunk;
                if (conn_read_exact(c, tmp, take) != 0) goto fail;
                BODY_APPEND(tmp, take);
                chunk -= take;
            }
            if (conn_read_line(c, line, sizeof(line)) < 0) goto fail;
        }
    } else if (r->content_length >= 0) {
        if ((uint64_t)r->content_length > max) goto too_big;
        uint64_t left = (uint64_t)r->content_length;
        while (left > 0) {
            size_t take = left > sizeof(tmp) ? sizeof(tmp) : (size_t)left;
            if (conn_read_exact(c, tmp, take) != 0) goto fail;
            BODY_APPEND(tmp, take);
            left -= take;
        }
    } else {
        /* Delimited by connection close. */
        c->alive = 0;
        for (;;) {
            size_t avail = c->rlen - c->rpos;
            if (avail == 0) {
                ssize_t n = conn_read_raw(c, c->rbuf, sizeof(c->rbuf));
                if (n <= 0) break;
                c->rpos = 0;
                c->rlen = (size_t)n;
                avail = (size_t)n;
            }
            if (len + avail > max) goto too_big;
            BODY_APPEND(c->rbuf + c->rpos, avail);
            c->rpos += avail;
        }
    }
#undef BODY_APPEND

    if (out) {
        if (!buf) buf = (char *)calloc(1, 1);
        else buf[len] = '\0';
        *out = buf;
    } else {
        free(buf);
    }
    if (out_len) *out_len = len;
    if (r->conn_close) c->alive = 0;
    return 0;

too_big:
    c->alive = 0;
    free(buf);
    return out ? -2 : 0;
fail:
    c->alive = 0;
    free(buf);
    return -1;
}

typedef struct {
    const char *method;      /* "GET" / "HEAD" */
    int has_range;
    uint64_t range_start;
    uint64_t range_len;
    void *range_buf;         /* receives up to range_len body bytes */
    char **body;             /* whole body (non-range GET) */
    size_t *body_len;
    size_t max_body;
} fetch_opts_t;

/* One request on *pc (reusing it when it talks to the same endpoint),
 * without redirects. A failure on a reused connection is retried once on
 * a fresh one (servers close idle keep-alive sockets). Returns the HTTP
 * status or negative on transport failure. */
static int fetch_on(http_conn_t **pc, const http_url_t *u, const http_source_config_t *cfg,
                    const fetch_opts_t *o, http_resp_t *resp, char *err, size_t err_sz) {
    for (int attempt = 0; attempt < 2; attempt++) {
        int reused = 0;
        if (*pc && (!(*pc)->alive || !same_endpoint(&(*pc)->ep, u))) {
            conn_close(*pc);
            *pc = NULL;
        }
        if (*pc) {
            reused = 1;
        } else {
            *pc = conn_open(u, cfg, NULL, err, err_sz);
            if (!*pc) return -1;
        }
        http_conn_t *c = *pc;

        uint64_t re = o->range_start + (o->range_len ? o->range_len - 1 : 0);
        if (http_send_request(c, u, cfg, o->method, o->has_range, o->range_start, re) != 0 ||
            http_read_response(c, resp) != 0) {
            conn_close(c);
            *pc = NULL;
            if (reused) continue;
            if (err[0] == '\0') snprintf(err, err_sz, "No valid HTTP response from %s", u->host);
            return -1;
        }

        int is_head = strcmp(o->method, "HEAD") == 0;
        int rc;
        if (o->range_buf && !is_head && (resp->status == 206 || resp->status == 200)) {
            /* 206: exact slice. 200: server ignored Range; only usable from
             * offset 0, and the rest of the body must be abandoned. */
            uint64_t avail = resp->content_length >= 0 ? (uint64_t)resp->content_length : o->range_len;
            if (resp->status == 206 && resp->has_range && resp->range_start != o->range_start) {
                c->alive = 0;
                snprintf(err, err_sz, "Server returned the wrong byte range");
                rc = -1;
            } else if (resp->status == 200 && o->range_start != 0) {
                c->alive = 0;
                rc = 0;
            } else {
                size_t want = (size_t)(avail < o->range_len ? avail : o->range_len);
                if (resp->chunked) {
                    /* Rare for ranges; fall back to buffering the body. */
                    char *body = NULL;
                    size_t blen = 0;
                    rc = http_read_body(c, resp, 0, &body, &blen, o->range_len + 65536);
                    if (rc == 0) {
                        want = blen < o->range_len ? blen : (size_t)o->range_len;
                        memcpy(o->range_buf, body, want);
                        resp->body_read = want;
                    }
                    free(body);
                } else {
                    rc = conn_read_exact(c, o->range_buf, want);
                    if (rc == 0) resp->body_read = want;
                    if (resp->status == 200 || (uint64_t)want != avail) c->alive = 0;
                }
            }
        } else if (o->body && !is_head && resp->status >= 200 && resp->status < 300) {
            rc = http_read_body(c, resp, 0, o->body, o->body_len, o->max_body);
            if (rc == -2) snprintf(err, err_sz, "Response too large");
        } else {
            rc = http_read_body(c, resp, is_head, NULL, NULL, 256 * 1024);
        }
        if (resp->conn_close) c->alive = 0;
        if (rc < 0) {
            conn_close(c);
            *pc = NULL;
            if (err[0] == '\0') snprintf(err, err_sz, "Connection lost while reading from %s", u->host);
            return -1;
        }
        if (!c->alive) {
            conn_close(c);
            *pc = NULL;
        }
        return resp->status;
    }
    return -1;
}

/* fetch_on + redirects. final_url (optional) receives the last URL used. */
static int http_fetch(http_conn_t **pc, const char *url, const http_source_config_t *cfg,
                      const fetch_opts_t *o, http_resp_t *resp,
                      char *final_url, size_t final_sz, char *err, size_t err_sz) {
    char cur[HTTP_URL_MAX];
    copy_str(cur, sizeof(cur), url);
    for (int hop = 0; hop <= HTTP_MAX_REDIRECTS; hop++) {
        http_url_t u;
        if (http_url_parse(cur, &u) != 0) {
            snprintf(err, err_sz, "Invalid URL: %.200s", cur);
            return -1;
        }
        int st = fetch_on(pc, &u, cfg, o, resp, err, err_sz);
        if (st < 0) return st;
        if ((st == 301 || st == 302 || st == 303 || st == 307 || st == 308) && resp->location[0]) {
            char next[HTTP_URL_MAX];
            if (url_resolve(cur, resp->location, next, sizeof(next)) != 0) {
                snprintf(err, err_sz, "Bad redirect target");
                return -1;
            }
            copy_str(cur, sizeof(cur), next);
            continue;
        }
        if (final_url) copy_str(final_url, final_sz, cur);
        return st;
    }
    snprintf(err, err_sz, "Too many redirects");
    return -1;
}

static const http_source_config_t *cfg_for(const char *url, http_source_config_t *storage) {
    return http_sources_find_for_url(url, storage) == 0 ? storage : NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * One-shot operations
 * ══════════════════════════════════════════════════════════════════════ */

static int range_probe(http_conn_t **pc, const char *url, const http_source_config_t *cfg,
                       uint64_t *out_total, char *final_url, size_t final_sz,
                       char *err, size_t err_sz) {
    unsigned char b[1];
    fetch_opts_t o = { "GET", 1, 0, 1, b, NULL, NULL, 0 };
    http_resp_t r;
    int st = http_fetch(pc, url, cfg, &o, &r, final_url, final_sz, err, err_sz);
    if (st == 206 && r.range_total > 0) {
        *out_total = r.range_total;
        return 1;
    }
    if (st == 200) return 0; /* reachable, but no byte ranges */
    if (st > 0 && err[0] == '\0') snprintf(err, err_sz, "HTTP %d", st);
    return -1;
}

int http_source_stat(const char *url, uint64_t *out_size, uint32_t *out_mtime) {
    http_source_config_t cs;
    const http_source_config_t *cfg = cfg_for(url, &cs);
    http_conn_t *conn = NULL;
    char err[256] = "";
    fetch_opts_t o = { "HEAD", 0, 0, 0, NULL, NULL, NULL, 0 };
    http_resp_t r;
    int st = http_fetch(&conn, url, cfg, &o, &r, NULL, 0, err, sizeof(err));
    int rc = -1;
    if (st == 200 && r.content_length >= 0) {
        if (out_size) *out_size = (uint64_t)r.content_length;
        if (out_mtime) *out_mtime = r.last_modified;
        rc = 0;
    } else if (st > 0) {
        /* Some servers reject HEAD; a one-byte range reveals the size. */
        uint64_t total = 0;
        err[0] = '\0';
        if (range_probe(&conn, url, cfg, &total, NULL, 0, err, sizeof(err)) == 1) {
            if (out_size) *out_size = total;
            if (out_mtime) *out_mtime = 0;
            rc = 0;
        }
    }
    if (conn) conn_close(conn);
    return rc;
}

ssize_t http_source_pread(const char *url, void *buf, size_t count, uint64_t offset) {
    if (!buf || count == 0) return 0;
    http_source_config_t cs;
    const http_source_config_t *cfg = cfg_for(url, &cs);
    http_conn_t *conn = NULL;
    char err[256] = "";
    fetch_opts_t o = { "GET", 1, offset, count, buf, NULL, NULL, 0 };
    http_resp_t r;
    int st = http_fetch(&conn, url, cfg, &o, &r, NULL, 0, err, sizeof(err));
    if (conn) conn_close(conn);
    if (st == 206 || (st == 200 && offset == 0)) return (ssize_t)r.body_read;
    return -1;
}

int http_source_calc_checksum(const char *url, char *out_checksum, size_t out_max) {
    if (!url || !out_checksum || out_max < 33) return -1;
    uint64_t size = 0;
    uint32_t mtime = 0;
    if (http_source_stat(url, &size, &mtime) != 0) return -1;
    uint8_t hdr[4096];
    size_t want = size < sizeof(hdr) ? (size_t)size : sizeof(hdr);
    ssize_t rd = want ? http_source_pread(url, hdr, want, 0) : 0;
    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)PKG_CACHE_FORMAT_TAG,
                                      sizeof(PKG_CACHE_FORMAT_TAG) - 1);
    if (rd > 0) crc = (uint32_t)mz_crc32(crc, hdr, (size_t)rd);
    snprintf(out_checksum, out_max, "%08x%016llx%08x", mtime, (unsigned long long)size, crc);
    return 0;
}

static ssize_t session_reader(void *ctx, void *buf, size_t count, uint64_t offset) {
    return http_file_session_read((http_file_session_t *)ctx, buf, count, offset);
}

int http_source_parse_pkg(const char *url, pkg_detail_t *out) {
    if (!url || !out) return -1;
    http_file_session_t *s = http_file_session_open(url);
    if (!s) return -1;
    int rc = pkg_parse_reader(session_reader, s, http_file_session_get_size(s), url, out);
    http_file_session_close(s);
    return rc;
}

int http_source_get_icon(const char *url, uint64_t offset, uint32_t size,
                         uint8_t **out_data, size_t *out_size) {
    if (!url || !out_data || !out_size) return -1;
    pkg_detail_t detail;
    if (offset == 0 || size == 0) {
        if (http_source_parse_pkg(url, &detail) != 0 || !detail.has_icon) return -1;
        offset = detail.icon_offset;
        size = detail.icon_size;
    }
    if (size == 0 || size >= 10 * 1024 * 1024) return -1;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) return -1;
    if (http_source_pread(url, buf, size, offset) != (ssize_t)size) {
        free(buf);
        return -1;
    }
    *out_data = buf;
    *out_size = size;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * Discovery (index.json / HTML autoindex)
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    const http_source_config_t *cfg;
    http_pkg_callback_t cb;
    void *user;
    int want_meta;           /* HEAD each package for size/mtime */
    int count;
    int dirs;
    http_conn_t *conn;
    char listing[16];
    char first_pkg[HTTP_URL_MAX];
    uint64_t *seen;
    size_t seen_count, seen_cap;
    char err[256];
} scan_ctx_t;

static int seen_add(scan_ctx_t *s, const char *url) {
    uint64_t h = fnv1a64(url);
    for (size_t i = 0; i < s->seen_count; i++) {
        if (s->seen[i] == h) return 0;
    }
    if (s->seen_count == s->seen_cap) {
        size_t ncap = s->seen_cap ? s->seen_cap * 2 : 256;
        uint64_t *n = (uint64_t *)realloc(s->seen, ncap * sizeof(uint64_t));
        if (!n) return 0;
        s->seen = n;
        s->seen_cap = ncap;
    }
    s->seen[s->seen_count++] = h;
    return 1;
}

static void emit_pkg(scan_ctx_t *s, const char *url, uint64_t size, uint32_t mtime, int have_meta) {
    if (!seen_add(s, url)) return;
    const char *slash = strrchr(url, '/');
    char name[256];
    pct_decode(slash ? slash + 1 : url, name, sizeof(name));
    if (!ends_with_ci(name, ".pkg")) return;

    if (s->want_meta && !have_meta) {
        char err[256] = "";
        fetch_opts_t o = { "HEAD", 0, 0, 0, NULL, NULL, NULL, 0 };
        http_resp_t r;
        int st = http_fetch(&s->conn, url, s->cfg, &o, &r, NULL, 0, err, sizeof(err));
        if (st == 200 && r.content_length >= 0) {
            size = (uint64_t)r.content_length;
            mtime = r.last_modified;
        } else {
            uint64_t total = 0;
            err[0] = '\0';
            if (range_probe(&s->conn, url, s->cfg, &total, NULL, 0, err, sizeof(err)) == 1) size = total;
        }
    }
    if (s->first_pkg[0] == '\0') copy_str(s->first_pkg, sizeof(s->first_pkg), url);
    s->count++;
    if (s->cb) s->cb(url, name, size, mtime, s->user);
}

/* index.json: {"files":[{"path":"PS4/Game.pkg","size":1,"mtime":2}, "PS5/x.pkg", ...]}
 * or a bare array. Paths are plain (not percent-encoded) and relative to
 * the source URL; absolute http(s) URLs are accepted as-is. */
static int scan_index_json(scan_ctx_t *s, const char *body, size_t len) {
    const char *end = body + len;
    const char *arr = json_value(body, end, "files");
    if (!arr) {
        arr = body;
        while (arr < end && isspace((unsigned char)*arr)) arr++;
    }
    if (arr >= end || *arr != '[') return -1;
    const char *arr_end = json_match(arr, end);
    if (!arr_end) return -1;

    const char *p = arr + 1;
    while (p < arr_end) {
        while (p < arr_end && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (p >= arr_end) break;
        char path[HTTP_URL_MAX] = "";
        uint64_t size = 0, mtime = 0;
        int have_size = 0;
        if (*p == '"') {
            p = json_parse_string(p, arr_end, path, sizeof(path));
            if (!p) break;
        } else if (*p == '{') {
            const char *oe = json_match(p, arr_end + 1);
            if (!oe) break;
            if (json_get_str(p, oe, "path", path, sizeof(path)) != 0 &&
                json_get_str(p, oe, "url", path, sizeof(path)) != 0) {
                json_get_str(p, oe, "name", path, sizeof(path));
            }
            have_size = json_get_u64(p, oe, "size", &size) == 0;
            json_get_u64(p, oe, "mtime", &mtime);
            p = oe + 1;
        } else {
            p++;
            continue;
        }
        if (path[0] == '\0') continue;

        char url[HTTP_URL_MAX];
        if (strncasecmp(path, "http://", 7) == 0 || strncasecmp(path, "https://", 8) == 0) {
            copy_str(url, sizeof(url), path);
        } else {
            char enc[HTTP_URL_MAX];
            const char *rel = path;
            while (*rel == '/') rel++;
            pct_encode_path(rel, enc, sizeof(enc));
            if (strlen(s->cfg->url) + strlen(enc) >= sizeof(url)) continue;
            snprintf(url, sizeof(url), "%s%s", s->cfg->url, enc);
        }
        emit_pkg(s, url, size, (uint32_t)mtime, have_size);
    }
    return 0;
}

static int scan_html_dir(scan_ctx_t *s, const char *dir_url, int depth) {
    if (depth > HTTP_MAX_DEPTH || s->dirs >= HTTP_MAX_DIRS) return 0;
    s->dirs++;

    char *body = NULL;
    size_t blen = 0;
    char err[256] = "";
    fetch_opts_t o = { "GET", 0, 0, 0, NULL, &body, &blen, HTTP_MAX_LISTING };
    http_resp_t r;
    int st = http_fetch(&s->conn, dir_url, s->cfg, &o, &r, NULL, 0, err, sizeof(err));
    if (st != 200 || !body) {
        free(body);
        if (depth == 0) {
            if (st > 0) snprintf(s->err, sizeof(s->err), "Listing %s returned HTTP %d", dir_url, st);
            else copy_str(s->err, sizeof(s->err), err);
            return -1;
        }
        return 0;
    }

    /* Collect sub-directories first so the listing buffer can be freed
     * before recursing (keeps peak memory at one listing per level). */
    char **subdirs = NULL;
    size_t nsub = 0, capsub = 0;

    for (const char *p = body; (p = strcasestr(p, "href")) != NULL;) {
        p += 4;
        while (*p == ' ') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ') p++;
        char quote = (*p == '"' || *p == '\'') ? *p++ : 0;
        const char *e = p;
        while (*e && (quote ? *e != quote : (*e != ' ' && *e != '>'))) e++;
        size_t hl = (size_t)(e - p);
        if (hl == 0 || hl >= HTTP_URL_MAX) {
            p = e;
            continue;
        }
        char href[HTTP_URL_MAX];
        memcpy(href, p, hl);
        href[hl] = '\0';
        p = e;
        html_unescape_inplace(href);

        if (href[0] == '?' || href[0] == '#' || strncasecmp(href, "mailto:", 7) == 0 ||
            strncasecmp(href, "javascript:", 11) == 0 || strncmp(href, "../", 3) == 0 ||
            strcmp(href, "..") == 0 || strcmp(href, "./") == 0) {
            continue;
        }
        char *q = strchr(href, '?');
        if (q) *q = '\0';

        char abs[HTTP_URL_MAX];
        if (url_resolve(dir_url, href, abs, sizeof(abs)) != 0) continue;
        /* Stay inside the current folder (and therefore inside the source). */
        size_t dl = strlen(dir_url);
        if (strncasecmp(abs, dir_url, dl) != 0 || strlen(abs) == dl) continue;

        size_t al = strlen(abs);
        if (abs[al - 1] == '/') {
            if (depth < HTTP_MAX_DEPTH && seen_add(s, abs)) {
                if (nsub == capsub) {
                    size_t ncap = capsub ? capsub * 2 : 16;
                    char **n = (char **)realloc(subdirs, ncap * sizeof(char *));
                    if (!n) continue;
                    subdirs = n;
                    capsub = ncap;
                }
                subdirs[nsub] = strdup(abs);
                if (subdirs[nsub]) nsub++;
            }
        } else {
            emit_pkg(s, abs, 0, 0, 0);
        }
    }
    free(body);

    for (size_t i = 0; i < nsub; i++) {
        scan_html_dir(s, subdirs[i], depth + 1);
        free(subdirs[i]);
    }
    free(subdirs);
    return 0;
}

static int scan_run(scan_ctx_t *s) {
    /* 1. index.json */
    char idx_url[HTTP_URL_MAX];
    snprintf(idx_url, sizeof(idx_url), "%sindex.json", s->cfg->url);
    char *body = NULL;
    size_t blen = 0;
    char err[256] = "";
    fetch_opts_t o = { "GET", 0, 0, 0, NULL, &body, &blen, HTTP_MAX_LISTING };
    http_resp_t r;
    int st = http_fetch(&s->conn, idx_url, s->cfg, &o, &r, NULL, 0, err, sizeof(err));
    if (st < 0) {
        free(body);
        copy_str(s->err, sizeof(s->err), err);
        return -1;
    }
    if (st == 200 && body && scan_index_json(s, body, blen) == 0) {
        free(body);
        copy_str(s->listing, sizeof(s->listing), "index.json");
        return s->count;
    }
    free(body);
    if (st == 401 || st == 403) {
        snprintf(s->err, sizeof(s->err), "Access denied (HTTP %d). Check username and password", st);
        return -1;
    }

    /* 2. HTML directory listing */
    if (seen_add(s, s->cfg->url) && scan_html_dir(s, s->cfg->url, 0) != 0) return -1;
    copy_str(s->listing, sizeof(s->listing), "html");
    return s->count;
}

static int scan_with(const http_source_config_t *cfg, http_pkg_callback_t cb, void *user,
                     int want_meta, scan_ctx_t *out_ctx) {
    scan_ctx_t s;
    memset(&s, 0, sizeof(s));
    s.cfg = cfg;
    s.cb = cb;
    s.user = user;
    s.want_meta = want_meta;
    int rc = scan_run(&s);
    if (s.conn) conn_close(s.conn);
    s.conn = NULL;
    free(s.seen);
    s.seen = NULL;
    if (rc < 0) install_log("[HTTP] Scan of %s failed: %s", cfg->url, s.err);
    if (out_ctx) *out_ctx = s;
    return rc;
}

int http_source_scan(const http_source_config_t *cfg, http_pkg_callback_t cb, void *user_data) {
    if (!cfg || !cfg->enabled) return 0;
    return scan_with(cfg, cb, user_data, 1, NULL);
}

int http_source_count_pkg_files(const http_source_config_t *cfg) {
    if (!cfg || !cfg->enabled) return 0;
    return scan_with(cfg, NULL, NULL, 0, NULL);
}

void http_source_test(const http_source_config_t *cfg_in, http_source_test_result_t *out) {
    memset(out, 0, sizeof(*out));
    out->range_supported = -1;
    http_source_config_t cfg = *cfg_in;
    cfg.enabled = 1;
    if (http_source_sanitize(&cfg) != 0) {
        copy_str(out->message, sizeof(out->message), "Invalid URL");
        return;
    }
    http_url_t u;
    http_url_parse(cfg.url, &u);
    if (u.https && !http_source_is_https_supported()) {
        copy_str(out->message, sizeof(out->message), "HTTPS is not supported by this build");
        return;
    }

#ifdef PKGMGR_HAVE_TLS
    if (u.https) {
        /* Read the certificate fingerprint without trusting it, so the UI
         * can offer "trust this certificate" even when verification fails. */
        char perr[256] = "";
        http_conn_t *probe = conn_open(&u, &cfg, HTTP_TLS_NONE, perr, sizeof(perr));
        if (probe) {
            copy_str(out->fingerprint, sizeof(out->fingerprint), probe->fingerprint);
            conn_close(probe);
        }
    }
#endif

    scan_ctx_t s;
    int n = scan_with(&cfg, NULL, NULL, 0, &s);
    if (n < 0) {
        copy_str(out->message, sizeof(out->message), s.err[0] ? s.err : "Source unreachable");
        return;
    }
    out->success = 1;
    out->pkg_count = n;
    copy_str(out->listing, sizeof(out->listing), s.listing);

    if (s.first_pkg[0]) {
        http_conn_t *conn = NULL;
        uint64_t total = 0;
        char err[256] = "";
        int rp = range_probe(&conn, s.first_pkg, &cfg, &total, NULL, 0, err, sizeof(err));
        if (conn) conn_close(conn);
        out->range_supported = rp == 1 ? 1 : 0;
    }

    const char *how = strcmp(s.listing, "index.json") == 0 ? "index.json" : "directory listing";
    if (n == 0) {
        snprintf(out->message, sizeof(out->message),
                 "Connected, but no .pkg files were found in the %s", how);
    } else if (out->range_supported == 0) {
        out->success = 0;
        snprintf(out->message, sizeof(out->message),
                 "Found %d package(s) via %s, but the server ignores byte-range requests "
                 "(Range). Enable range support on the server", n, how);
    } else {
        snprintf(out->message, sizeof(out->message),
                 "Connected. Found %d package(s) via %s", n, how);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * Streaming session (pooled keep-alive connections)
 * ══════════════════════════════════════════════════════════════════════ */

struct http_file_session {
    char url[HTTP_URL_MAX];      /* final URL after redirects */
    http_url_t u;
    http_source_config_t cfg;
    int has_cfg;
    uint64_t size;
    pthread_mutex_t lock;
    http_conn_t *idle[HTTP_POOL_MAX];
    int idle_count;
};

static http_conn_t *session_take(http_file_session_t *s) {
    http_conn_t *c = NULL;
    pthread_mutex_lock(&s->lock);
    if (s->idle_count > 0) c = s->idle[--s->idle_count];
    pthread_mutex_unlock(&s->lock);
    return c;
}

static void session_give(http_file_session_t *s, http_conn_t *c) {
    if (!c) return;
    if (!c->alive) {
        conn_close(c);
        return;
    }
    pthread_mutex_lock(&s->lock);
    if (s->idle_count < HTTP_POOL_MAX) {
        s->idle[s->idle_count++] = c;
        c = NULL;
    }
    pthread_mutex_unlock(&s->lock);
    if (c) conn_close(c);
}

http_file_session_t *http_file_session_open(const char *url) {
    if (!url) return NULL;
    http_file_session_t *s = (http_file_session_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutex_init(&s->lock, NULL);
    s->has_cfg = http_sources_find_for_url(url, &s->cfg) == 0;

    http_conn_t *conn = NULL;
    char err[256] = "";
    uint64_t total = 0;
    int rp = range_probe(&conn, url, s->has_cfg ? &s->cfg : NULL, &total,
                         s->url, sizeof(s->url), err, sizeof(err));
    if (rp != 1) {
        if (conn) conn_close(conn);
        install_log("[HTTP] Cannot stream %.300s: %s", url,
                    rp == 0 ? "server does not support byte ranges" : err);
        pthread_mutex_destroy(&s->lock);
        free(s);
        return NULL;
    }
    s->size = total;
    http_url_parse(s->url, &s->u);
    session_give(s, conn);
    return s;
}

ssize_t http_file_session_read(http_file_session_t *s, void *buf, size_t count, uint64_t offset) {
    if (!s || !buf) return -1;
    if (count == 0 || offset >= s->size) return 0;
    if (count > s->size - offset) count = (size_t)(s->size - offset);

    http_conn_t *c = session_take(s);
    char err[256] = "";
    fetch_opts_t o = { "GET", 1, offset, count, buf, NULL, NULL, 0 };
    http_resp_t r;
    int st = fetch_on(&c, &s->u, s->has_cfg ? &s->cfg : NULL, &o, &r, err, sizeof(err));
    session_give(s, c);
    if (st == 206 && r.body_read > 0) return (ssize_t)r.body_read;
    install_log("[HTTP] Range read %llu+%zu failed: %s",
                (unsigned long long)offset, count, st > 0 ? "unexpected HTTP status" : err);
    return -1;
}

uint64_t http_file_session_get_size(http_file_session_t *s) {
    return s ? s->size : 0;
}

void http_file_session_close(http_file_session_t *s) {
    if (!s) return;
    pthread_mutex_lock(&s->lock);
    for (int i = 0; i < s->idle_count; i++) conn_close(s->idle[i]);
    s->idle_count = 0;
    pthread_mutex_unlock(&s->lock);
    pthread_mutex_destroy(&s->lock);
    free(s);
}
