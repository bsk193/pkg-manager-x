/*
 * PKG Manager X - REST routes for HTTP sources and platform info
 */

#include "http_sources_api.h"
#include "http_source.h"
#include "pkg_platform.h"
#include "installer.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static enum MHD_Result send_json(struct MHD_Connection *conn, unsigned int status, char *json) {
    if (!json) json = strdup("{\"success\":false,\"error\":\"Out of memory\"}");
    struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(json), json, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(resp, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(resp, "Access-Control-Allow-Headers", "Content-Type, Range");
    MHD_add_response_header(resp, "Content-Type", "application/json");
    MHD_add_response_header(resp, "Cache-Control", "no-store");
    enum MHD_Result ret = MHD_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

static void esc(const char *src, char *dst, size_t dst_sz) {
    size_t d = 0;
    for (size_t s = 0; src && src[s] && d + 7 < dst_sz; s++) {
        unsigned char c = (unsigned char)src[s];
        if (c == '"' || c == '\\') { dst[d++] = '\\'; dst[d++] = (char)c; }
        else if (c < 32) { d += (size_t)snprintf(dst + d, dst_sz - d, "\\u%04x", c); }
        else dst[d++] = (char)c;
    }
    dst[d] = '\0';
}

/* Blank password in a POST means "keep the stored one" (GET never echoes
 * passwords). Match by id, else by URL + username. */
static void inherit_password(http_source_config_t *c, const http_source_config_t *old, int old_count) {
    if (c->password[0] != '\0') return;
    for (int j = 0; j < old_count; j++) {
        int same = (c->id[0] && strcmp(c->id, old[j].id) == 0) ||
                   (strcasecmp(c->url, old[j].url) == 0 && strcmp(c->username, old[j].username) == 0);
        if (same && old[j].password[0]) {
            snprintf(c->password, sizeof(c->password), "%s", old[j].password);
            return;
        }
    }
}

static char *platform_json(void) {
    const char *console = pkg_platform_console();
    int is_ps4 = strcmp(console, "ps4") == 0;
    char *json = (char *)malloc(256);
    if (!json) return NULL;
    snprintf(json, 256,
             "{\"console\":\"%s\",\"can_install\":%s,\"https_supported\":%s,\"shortcut_supported\":%s}",
             console, is_ps4 ? "[\"ps4\"]" : "[\"ps4\",\"ps5\"]",
             http_source_is_https_supported() ? "true" : "false",
             is_ps4 ? "false" : "true");
    return json;
}

int http_sources_api_handle(struct MHD_Connection *conn, const char *url, const char *method,
                            const char *body, enum MHD_Result *out_ret) {
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/platform") == 0) {
        *out_ret = send_json(conn, MHD_HTTP_OK, platform_json());
        return 1;
    }

    if (strcmp(url, "/api/http/sources") == 0) {
        if (strcmp(method, "GET") == 0) {
            *out_ret = send_json(conn, MHD_HTTP_OK, http_sources_to_json());
            return 1;
        }
        if (strcmp(method, "POST") == 0) {
            http_source_config_t old[MAX_HTTP_SOURCES], list[MAX_HTTP_SOURCES];
            int old_count = http_sources_get(old, MAX_HTTP_SOURCES);
            int n = http_sources_parse_json(body ? body : "", list, MAX_HTTP_SOURCES);
            if (!body || !strstr(body, "\"sources\"")) {
                *out_ret = send_json(conn, MHD_HTTP_BAD_REQUEST,
                                     strdup("{\"success\":false,\"error\":\"Expected {\\\"sources\\\":[...]}\"}"));
                return 1;
            }
            for (int i = 0; i < n; i++) {
                inherit_password(&list[i], old, old_count);
                if (http_source_sanitize(&list[i]) != 0) {
                    char e_url[600], msg[800];
                    esc(list[i].url, e_url, sizeof(e_url));
                    snprintf(msg, sizeof(msg),
                             "{\"success\":false,\"error\":\"Invalid or too long URL: %s\"}", e_url);
                    *out_ret = send_json(conn, MHD_HTTP_BAD_REQUEST, strdup(msg));
                    return 1;
                }
            }
            int saved = http_sources_set(list, n);
            install_log("[HTTP] Saved %d HTTP source(s)", saved);
            char msg[64];
            snprintf(msg, sizeof(msg), "{\"success\":true,\"count\":%d}", saved);
            *out_ret = send_json(conn, MHD_HTTP_OK, strdup(msg));
            return 1;
        }
    }

    if (strcmp(method, "POST") == 0 && strcmp(url, "/api/http/test") == 0) {
        http_source_config_t cfg[1];
        if (http_sources_parse_json(body ? body : "", cfg, 1) != 1) {
            *out_ret = send_json(conn, MHD_HTTP_BAD_REQUEST,
                                 strdup("{\"success\":false,\"message\":\"URL is required\"}"));
            return 1;
        }
        http_source_config_t old[MAX_HTTP_SOURCES];
        int old_count = http_sources_get(old, MAX_HTTP_SOURCES);
        inherit_password(&cfg[0], old, old_count);

        install_log("[HTTP] POST /api/http/test: url='%s' tls=%s user='%s'",
                    cfg[0].url, cfg[0].tls_mode[0] ? cfg[0].tls_mode : "verify",
                    cfg[0].username[0] ? cfg[0].username : "(none)");
        http_source_test_result_t r;
        http_source_test(&cfg[0], &r);
        install_log("[HTTP] POST /api/http/test: success=%d pkgs=%d ranges=%d listing=%s msg='%s'",
                    r.success, r.pkg_count, r.range_supported, r.listing, r.message);

        char e_msg[1100], e_fp[220], e_listing[40];
        esc(r.message, e_msg, sizeof(e_msg));
        esc(r.fingerprint, e_fp, sizeof(e_fp));
        esc(r.listing, e_listing, sizeof(e_listing));
        char *json = (char *)malloc(1600);
        if (json) {
            snprintf(json, 1600,
                     "{\"success\":%s,\"message\":\"%s\",\"pkg_count\":%d,\"range_supported\":%s,"
                     "\"listing\":\"%s\",\"fingerprint\":\"%s\"}",
                     r.success ? "true" : "false", e_msg, r.pkg_count,
                     r.range_supported < 0 ? "null" : (r.range_supported ? "true" : "false"),
                     e_listing, e_fp);
        }
        *out_ret = send_json(conn, MHD_HTTP_OK, json);
        return 1;
    }

    return 0;
}
