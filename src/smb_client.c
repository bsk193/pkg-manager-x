/*
 * PKG Manager - SMB2 Network Client
 *
 * Implements mount-free SMB2 directory scanning, metadata parsing,
 * icon extraction, and network range-streaming via libsmb2.
 */

#include "smb_client.h"
#include "pkg_cache.h"
#include "pkg_parser.h"
#include "icon_blurhash.h"
#include "multipart.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/smb2-errors.h>
#include "installer.h"

/* Helper for reading big-endian / little-endian integers */
static inline uint32_t smb_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint16_t smb_read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t smb_read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t smb_read_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)p[i]) << (i * 8);
    }
    return v;
}

/* Parse smb://[server][:port]/[share]/[path] */
int smb_client_parse_url(const char *smb_url,
                         char *out_server, size_t server_sz,
                         int *out_port,
                         char *out_share, size_t share_sz,
                         char *out_path, size_t path_sz) {
    if (!smb_url || strncmp(smb_url, "smb://", 6) != 0) {
        return -1;
    }

    if (out_server && server_sz > 0) out_server[0] = '\0';
    if (out_share && share_sz > 0) out_share[0] = '\0';
    if (out_path && path_sz > 0) out_path[0] = '\0';
    if (out_port) *out_port = SMB_DEFAULT_PORT;

    const char *p = smb_url + 6; /* skip "smb://" */

    /* Extract host[:port] */
    const char *slash = strchr(p, '/');
    char host_port[160];
    if (slash) {
        size_t hlen = (size_t)(slash - p);
        if (hlen >= sizeof(host_port)) hlen = sizeof(host_port) - 1;
        strncpy(host_port, p, hlen);
        host_port[hlen] = '\0';
        p = slash + 1;
    } else {
        strncpy(host_port, p, sizeof(host_port) - 1);
        host_port[sizeof(host_port) - 1] = '\0';
        p = "";
    }

    /* Check for colon port in host_port */
    char *colon = strchr(host_port, ':');
    if (colon) {
        *colon = '\0';
        int prt = atoi(colon + 1);
        if (prt > 0 && out_port) {
            *out_port = prt;
        }
    }

    if (out_server && server_sz > 0) {
        strncpy(out_server, host_port, server_sz - 1);
        out_server[server_sz - 1] = '\0';
    }

    /* Extract share and path */
    if (*p != '\0') {
        const char *next_slash = strchr(p, '/');
        if (next_slash) {
            size_t slen = (size_t)(next_slash - p);
            if (out_share && share_sz > 0) {
                if (slen >= share_sz) slen = share_sz - 1;
                strncpy(out_share, p, slen);
                out_share[slen] = '\0';
            }
            if (out_path && path_sz > 0) {
                const char *rel = next_slash;
                while (*rel == '/') rel++;
                strncpy(out_path, rel, path_sz - 1);
                out_path[path_sz - 1] = '\0';
            }
        } else {
            if (out_share && share_sz > 0) {
                strncpy(out_share, p, share_sz - 1);
                out_share[share_sz - 1] = '\0';
            }
            if (out_path && path_sz > 0) {
                out_path[0] = '\0';
            }
        }
    }

    return 0;
}

int smb_client_find_share_cfg(const char *smb_url, smb_share_config_t *out_cfg) {
    if (!smb_url || !out_cfg) return -1;

    char srv[128] = {0};
    char shr[128] = {0};
    char rel[256] = {0};
    int prt = SMB_DEFAULT_PORT;

    if (smb_client_parse_url(smb_url, srv, sizeof(srv), &prt, shr, sizeof(shr), rel, sizeof(rel)) != 0) {
        return -1;
    }

    app_settings_t settings;
    pkg_cache_get_settings(&settings);

    for (int i = 0; i < settings.smb_share_count; i++) {
        smb_share_config_t *c = &settings.smb_shares[i];
        if (!c->enabled) continue;

        smb_share_config_t clean_c = *c;
        smb_client_sanitize_config(&clean_c);

        /* Match by server & share */
        if (strcasecmp(clean_c.server, srv) == 0 && strcasecmp(clean_c.share, shr) == 0) {
            memcpy(out_cfg, c, sizeof(smb_share_config_t));
            return 0;
        }
        /* Or match if server matches drive id, e.g. smb://smb0/... */
        if (strcasecmp(clean_c.id, srv) == 0) {
            memcpy(out_cfg, c, sizeof(smb_share_config_t));
            return 0;
        }
    }

    /* Fallback: if no credentials matched, return a guest config for that server/share */
    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->enabled = 1;
    strncpy(out_cfg->server, srv, sizeof(out_cfg->server) - 1);
    out_cfg->port = prt;
    strncpy(out_cfg->share, shr, sizeof(out_cfg->share) - 1);
    strncpy(out_cfg->workgroup, "WORKGROUP", sizeof(out_cfg->workgroup) - 1);
    snprintf(out_cfg->label, sizeof(out_cfg->label), "%.30s/%.30s", srv, shr);
    return 0;
}

void smb_client_sanitize_config(smb_share_config_t *cfg) {
    if (!cfg) return;

    /* 1. Sanitize server: trim whitespace, strip smb:// and leading/trailing slashes */
    char *s = cfg->server;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (strncasecmp(s, "smb://", 6) == 0) s += 6;
    while (*s == '/' || *s == '\\') s++;

    char clean_srv[sizeof(cfg->server)];
    strncpy(clean_srv, s, sizeof(clean_srv) - 1);
    clean_srv[sizeof(clean_srv) - 1] = '\0';

    /* Trim trailing whitespace and slashes */
    size_t slen = strlen(clean_srv);
    while (slen > 0 && (clean_srv[slen - 1] == ' ' || clean_srv[slen - 1] == '\t' ||
                        clean_srv[slen - 1] == '\r' || clean_srv[slen - 1] == '\n' ||
                        clean_srv[slen - 1] == '/' || clean_srv[slen - 1] == '\\')) {
        clean_srv[--slen] = '\0';
    }

    char extracted_subpath[sizeof(cfg->path)] = {0};
    /* If server string contains a slash/backslash (e.g. host/share or host/share/path), split it */
    char *slash = strpbrk(clean_srv, "/\\");
    if (slash) {
        *slash = '\0';
        char *extra = slash + 1;
        while (*extra == '/' || *extra == '\\') extra++;

        if (*extra != '\0') {
            char *next_slash = strpbrk(extra, "/\\");
            char extracted_share[sizeof(cfg->share)] = {0};
            char *subp = NULL;

            if (next_slash) {
                *next_slash = '\0';
                strncpy(extracted_share, extra, sizeof(extracted_share) - 1);
                subp = next_slash + 1;
                while (*subp == '/' || *subp == '\\') subp++;
            } else {
                strncpy(extracted_share, extra, sizeof(extracted_share) - 1);
            }

            if (cfg->share[0] == '\0') {
                strncpy(cfg->share, extracted_share, sizeof(cfg->share) - 1);
                cfg->share[sizeof(cfg->share) - 1] = '\0';
            }

            if (subp && *subp != '\0') {
                strncpy(extracted_subpath, subp, sizeof(extracted_subpath) - 1);
                extracted_subpath[sizeof(extracted_subpath) - 1] = '\0';
            }
        }
    }

    /* If server has embedded port e.g. 192.168.1.100:445 or [::1]:445 */
    char *first_colon = strchr(clean_srv, ':');
    char *last_colon = strrchr(clean_srv, ':');
    char *port_colon = NULL;

    if (first_colon && first_colon == last_colon) {
        /* Exactly one colon -> host:port */
        port_colon = first_colon;
    } else if (first_colon && first_colon != last_colon) {
        /* Multiple colons -> IPv6. Port only exists if after ']' e.g. [fe80::1]:445 */
        char *bracket = strrchr(clean_srv, ']');
        if (bracket && last_colon > bracket) {
            port_colon = last_colon;
        }
    }

    if (port_colon && port_colon > clean_srv) {
        int is_all_digits = 1;
        for (char *cp = port_colon + 1; *cp; cp++) {
            if (*cp < '0' || *cp > '9') { is_all_digits = 0; break; }
        }
        if (is_all_digits && *(port_colon + 1) != '\0') {
            int p = atoi(port_colon + 1);
            if (p > 0 && (cfg->port <= 0 || cfg->port == SMB_DEFAULT_PORT)) {
                cfg->port = p;
            }
            *port_colon = '\0';
        }
    }

    strncpy(cfg->server, clean_srv, sizeof(cfg->server) - 1);
    cfg->server[sizeof(cfg->server) - 1] = '\0';

    /* 2. Sanitize share */
    char *sh = cfg->share;
    while (*sh == ' ' || *sh == '\t' || *sh == '\r' || *sh == '\n' || *sh == '/' || *sh == '\\') sh++;
    char clean_sh[sizeof(cfg->share)];
    strncpy(clean_sh, sh, sizeof(clean_sh) - 1);
    clean_sh[sizeof(clean_sh) - 1] = '\0';
    size_t shlen = strlen(clean_sh);
    while (shlen > 0 && (clean_sh[shlen - 1] == ' ' || clean_sh[shlen - 1] == '\t' ||
                         clean_sh[shlen - 1] == '\r' || clean_sh[shlen - 1] == '\n' ||
                         clean_sh[shlen - 1] == '/' || clean_sh[shlen - 1] == '\\')) {
        clean_sh[--shlen] = '\0';
    }
    strncpy(cfg->share, clean_sh, sizeof(cfg->share) - 1);
    cfg->share[sizeof(cfg->share) - 1] = '\0';

    /* 3. Sanitize path: normalize backslashes to slashes, collapse duplicates, strip leading/trailing */
    char *p = cfg->path;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '/' || *p == '\\') p++;
    char clean_pth[sizeof(cfg->path)];
    size_t d = 0;
    for (; *p && d + 1 < sizeof(clean_pth); p++) {
        char ch = (*p == '\\') ? '/' : *p;
        if (ch == '/' && d > 0 && clean_pth[d - 1] == '/') continue;
        clean_pth[d++] = ch;
    }
    clean_pth[d] = '\0';
    while (d > 0 && (clean_pth[d - 1] == ' ' || clean_pth[d - 1] == '\t' ||
                     clean_pth[d - 1] == '\r' || clean_pth[d - 1] == '\n' ||
                     clean_pth[d - 1] == '/' || clean_pth[d - 1] == '\\')) {
        clean_pth[--d] = '\0';
    }

    /* Clean extracted_subpath as well if present */
    if (extracted_subpath[0] != '\0') {
        char *sp = extracted_subpath;
        while (*sp == ' ' || *sp == '\t' || *sp == '\r' || *sp == '\n' || *sp == '/' || *sp == '\\') sp++;
        char clean_sub[sizeof(cfg->path)];
        size_t sd = 0;
        for (; *sp && sd + 1 < sizeof(clean_sub); sp++) {
            char ch = (*sp == '\\') ? '/' : *sp;
            if (ch == '/' && sd > 0 && clean_sub[sd - 1] == '/') continue;
            clean_sub[sd++] = ch;
        }
        clean_sub[sd] = '\0';
        while (sd > 0 && (clean_sub[sd - 1] == ' ' || clean_sub[sd - 1] == '\t' ||
                         clean_sub[sd - 1] == '\r' || clean_sub[sd - 1] == '\n' ||
                         clean_sub[sd - 1] == '/' || clean_sub[sd - 1] == '\\')) {
            clean_sub[--sd] = '\0';
        }
        if (clean_sub[0] != '\0') {
            if (clean_pth[0] == '\0') {
                strncpy(clean_pth, clean_sub, sizeof(clean_pth) - 1);
                clean_pth[sizeof(clean_pth) - 1] = '\0';
            } else {
                char joined[sizeof(cfg->path)];
                snprintf(joined, sizeof(joined), "%s/%s", clean_sub, clean_pth);
                strncpy(clean_pth, joined, sizeof(clean_pth) - 1);
                clean_pth[sizeof(clean_pth) - 1] = '\0';
            }
        }
    }

    strncpy(cfg->path, clean_pth, sizeof(cfg->path) - 1);
    cfg->path[sizeof(cfg->path) - 1] = '\0';

    /* 4. Sanitize username: trim whitespace */
    char *u = cfg->username;
    while (*u == ' ' || *u == '\t') u++;
    if (u != cfg->username) {
        memmove(cfg->username, u, strlen(u) + 1);
    }
    size_t ulen = strlen(cfg->username);
    while (ulen > 0 && (cfg->username[ulen - 1] == ' ' || cfg->username[ulen - 1] == '\t' ||
                        cfg->username[ulen - 1] == '\r' || cfg->username[ulen - 1] == '\n')) {
        cfg->username[--ulen] = '\0';
    }

    /* 5. Workgroup default */
    char *w = cfg->workgroup;
    while (*w == ' ' || *w == '\t') w++;
    if (w != cfg->workgroup) {
        memmove(cfg->workgroup, w, strlen(w) + 1);
    }
    size_t wlen = strlen(cfg->workgroup);
    while (wlen > 0 && (cfg->workgroup[wlen - 1] == ' ' || cfg->workgroup[wlen - 1] == '\t' ||
                        cfg->workgroup[wlen - 1] == '\r' || cfg->workgroup[wlen - 1] == '\n')) {
        cfg->workgroup[--wlen] = '\0';
    }
    if (cfg->workgroup[0] == '\0') {
        strncpy(cfg->workgroup, "WORKGROUP", sizeof(cfg->workgroup) - 1);
        cfg->workgroup[sizeof(cfg->workgroup) - 1] = '\0';
    }

    /* 6. Port default */
    if (cfg->port <= 0) {
        cfg->port = SMB_DEFAULT_PORT;
    }
}

static inline uint64_t smb_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static void smb_test_error_cb(struct smb2_context *smb2, const char *error_string) {
    (void)smb2;
    if (error_string && *error_string) {
        install_log("[SMB TEST] libsmb2 callback: %s", error_string);
    }
}

static void smb_log_nt_diagnostic(uint32_t nt_err, const char *server, const char *share) {
    if (nt_err == 0xC000015B) {
        install_log("[SMB TEST] -> DIAGNOSTIC: NT status 0x%08X (STATUS_LOGON_TYPE_NOT_GRANTED).", nt_err);
        install_log("[SMB TEST] -> Windows security policy blocks network logons for this account.");
        install_log("[SMB TEST] -> On Windows 10/11, 'Guest' is in 'Deny access to this computer from the network' by default.");
        install_log("[SMB TEST] -> FIX 1: Open secpol.msc on Windows -> Local Policies -> User Rights Assignment -> double-click 'Deny access to this computer from the network' -> select 'Guest' -> click Remove.");
        install_log("[SMB TEST] -> FIX 2 (Recommended): In Settings, enter a local Windows user account and password instead of Guest.");
    } else if (nt_err == 0xC0000072 || nt_err == 0xC000006D || nt_err == 0xC000006E) {
        install_log("[SMB TEST] -> DIAGNOSTIC: NT status 0x%08X (%s).", nt_err, nterror_to_str(nt_err));
        install_log("[SMB TEST] -> On Windows 10/11, the local 'Guest' account is DISABLED by default.");
        install_log("[SMB TEST] -> FIX 1 (Enable Guest): On the Windows PC, open PowerShell/CMD as Admin and run: 'net user Guest /active:yes'.");
        install_log("[SMB TEST] -> FIX 2 (Use Windows User): In Settings, enter your local Windows username and password.");
    } else if (nt_err == 0xC0000022) {
        install_log("[SMB TEST] -> DIAGNOSTIC: NT status 0x%08X (STATUS_ACCESS_DENIED).", nt_err);
        install_log("[SMB TEST] -> Windows denied anonymous access or folder permissions lack 'Everyone'/'Guest'.");
        install_log("[SMB TEST] -> FIX: In Windows folder Properties -> Security tab -> Edit -> Add 'Everyone' and 'Guest' with Read permissions, or enter a Windows account with password.");
    } else if (nt_err == 0xC00000CC) {
        install_log("[SMB TEST] -> DIAGNOSTIC: NT status 0x%08X (STATUS_BAD_NETWORK_NAME).", nt_err);
        install_log("[SMB TEST] -> Share '%s' was not found on '%s'. Verify the exact share name (use the SMB share name, not a Windows path).",
                    share ? share : "", server ? server : "");
    } else if (nt_err == 0xC000000D) {
        install_log("[SMB TEST] -> DIAGNOSTIC: NT status 0x%08X (STATUS_INVALID_PARAMETER).", nt_err);
        install_log("[SMB TEST] -> Windows rejected anonymous logon (common post-KB5026436 where SMB signing is required).");
    } else if (nt_err == 0xC0000203) {
        install_log("[SMB TEST] -> DIAGNOSTIC: NT status 0x%08X (STATUS_USER_SESSION_DELETED). Server closed the SMB session.", nt_err);
    } else if (nt_err != 0) {
        install_log("[SMB TEST] -> DIAGNOSTIC: NT status 0x%08X (%s).", nt_err, nterror_to_str(nt_err));
    }
}

/* Helper to connect to an SMB share using smb2_context */
static struct smb2_context *smb_connect(const smb_share_config_t *cfg, char *out_err, size_t err_sz, int verbose) {
    if (!cfg) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "Invalid share configuration (missing server/share)");
        if (verbose) install_log("[SMB TEST] ERROR: Invalid share configuration (NULL cfg)");
        return NULL;
    }

    smb_share_config_t clean_cfg = *cfg;
    smb_client_sanitize_config(&clean_cfg);

    if (clean_cfg.server[0] == '\0' || clean_cfg.share[0] == '\0') {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "Invalid share configuration (missing server/share)");
        if (verbose) install_log("[SMB TEST] ERROR: Missing server or share (server='%s', share='%s')", clean_cfg.server, clean_cfg.share);
        return NULL;
    }

    struct smb2_context *ctx = smb2_init_context();
    if (!ctx) {
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "Failed to allocate SMB2 context");
        if (verbose) install_log("[SMB TEST] ERROR: Failed to allocate SMB2 context");
        return NULL;
    }

    smb2_set_timeout(ctx, 10);
    smb2_set_security_mode(ctx, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (verbose) {
        smb2_register_error_callback(ctx, smb_test_error_cb);
    }

    const char *user = (clean_cfg.username[0] != '\0') ? clean_cfg.username : "Guest";
    smb2_set_user(ctx, user);
    if (clean_cfg.password[0] != '\0') smb2_set_password(ctx, clean_cfg.password);
    smb2_set_domain(ctx, (clean_cfg.workgroup[0] != '\0') ? clean_cfg.workgroup : "WORKGROUP");

    char srv_buf[192];
    if (clean_cfg.port > 0 && clean_cfg.port != SMB_DEFAULT_PORT) {
        snprintf(srv_buf, sizeof(srv_buf), "%s:%d", clean_cfg.server, clean_cfg.port);
    } else {
        snprintf(srv_buf, sizeof(srv_buf), "%s", clean_cfg.server);
    }

    if (verbose) {
        install_log("[SMB TEST] Attempting connection -> server='%s', share='%s', user='%s', domain='%s', pass=%s, sec_mode=SIGNING_ENABLED",
                    srv_buf, clean_cfg.share, user,
                    clean_cfg.workgroup[0] ? clean_cfg.workgroup : "WORKGROUP",
                    clean_cfg.password[0] ? "(configured)" : "(none)");
    }

    uint64_t t0 = smb_now_ms();
    int rc = smb2_connect_share(ctx, srv_buf, clean_cfg.share, user);
    uint64_t elapsed_ms = smb_now_ms() - t0;

    if (rc != 0) {
        const char *err = smb2_get_error(ctx);
        if (!err || !*err) err = "Failed to connect to SMB share";
        uint32_t nt_err = (uint32_t)smb2_get_nterror(ctx);
        const char *nt_str = nterror_to_str(nt_err);

        if (verbose) {
            install_log("[SMB TEST] -> smb2_connect_share FAILED (rc=%d, nt_status=0x%08X [%s], error='%s', elapsed=%llums)",
                        rc, (unsigned int)nt_err, nt_str ? nt_str : "UNKNOWN", err, (unsigned long long)elapsed_ms);
            smb_log_nt_diagnostic(nt_err, clean_cfg.server, clean_cfg.share);
        }

        if (out_err && err_sz > 0) {
            if (nt_err == 0xC000015B) {
                snprintf(out_err, err_sz, "Logon type not granted (0x%08X): Windows policy blocks this account from network logon. In secpol.msc, remove Guest from 'Deny access to this computer from the network', or enter Windows credentials.",
                         nt_err);
            } else if (nt_err == 0xC000006D || nt_err == 0xC0000072 || nt_err == 0xC000006E) {
                snprintf(out_err, err_sz, "Logon rejected (0x%08X %s): Windows rejected the logon (Guest account is disabled by default on Windows 10/11). Enable it ('net user Guest /active:yes' in Windows) or enter Windows credentials.",
                         nt_err, nt_str ? nt_str : "STATUS_LOGON_FAILURE");
            } else if (nt_err == 0xC0000022) {
                snprintf(out_err, err_sz, "Access denied (0x%08X STATUS_ACCESS_DENIED): Windows denied unauthenticated access. Verify folder NTFS & Share permissions grant access, or enter Windows credentials.",
                         nt_err);
            } else if (nt_err == 0xC00000CC) {
                snprintf(out_err, err_sz, "Share '%s' not found on '%s' (0x%08X STATUS_BAD_NETWORK_NAME)",
                         clean_cfg.share, clean_cfg.server, nt_err);
            } else if (nt_err != 0) {
                snprintf(out_err, err_sz, "%s (0x%08X)",
                         nt_str ? nt_str : "SMB error", nt_err);
            } else {
                snprintf(out_err, err_sz, "%s", err);
            }
        }
        smb2_destroy_context(ctx);
        return NULL;
    }

    if (verbose) {
        install_log("[SMB TEST] -> smb2_connect_share SUCCEEDED (rc=0, elapsed=%llums)",
                    (unsigned long long)elapsed_ms);
    }

    return ctx;
}

int smb_client_test_connection(const smb_share_config_t *cfg, char *out_err, size_t err_sz) {
    install_log("[SMB TEST] ==================== SMB CONNECTION TEST START ====================");
    if (!cfg) {
        install_log("[SMB TEST] ERROR: No configuration provided (cfg is NULL)");
        install_log("[SMB TEST] ==================== SMB CONNECTION TEST RESULT: FAILED ====================");
        if (out_err && err_sz > 0) snprintf(out_err, err_sz, "No configuration provided");
        return -1;
    }

    smb_share_config_t clean_cfg = *cfg;
    smb_client_sanitize_config(&clean_cfg);

    install_log("[SMB TEST] Target: smb://%s:%d/%s (subpath: '%s')",
                clean_cfg.server,
                clean_cfg.port > 0 ? clean_cfg.port : SMB_DEFAULT_PORT,
                clean_cfg.share,
                clean_cfg.path[0] ? clean_cfg.path : "/");
    install_log("[SMB TEST] Config: user='%s', workgroup='%s', password=%s, read_only=%d",
                clean_cfg.username[0] ? clean_cfg.username : "(none/guest)",
                clean_cfg.workgroup[0] ? clean_cfg.workgroup : "(none)",
                clean_cfg.password[0] ? "(configured)" : "(none)",
                clean_cfg.is_read_only);

    struct smb2_context *ctx = smb_connect(&clean_cfg, out_err, err_sz, 1);
    if (!ctx) {
        install_log("[SMB TEST] Connection FAILED: %s", (out_err && *out_err) ? out_err : "Unknown error");
        install_log("[SMB TEST] ==================== SMB CONNECTION TEST RESULT: FAILED ====================");
        return -1;
    }

    install_log("[SMB TEST] Successfully connected to SMB share '%s'!", clean_cfg.share);

    /* Verify target path */
    const char *target_dir = clean_cfg.path;
    install_log("[SMB TEST] Verifying folder access at path: '%s'...", target_dir[0] ? target_dir : "/");

    struct smb2dir *dir = smb2_opendir(ctx, target_dir);
    if (!dir) {
        const char *err = smb2_get_error(ctx);
        if (!err || !*err) err = "Access denied or folder not found";
        uint32_t nt_err = (uint32_t)smb2_get_nterror(ctx);
        install_log("[SMB TEST] smb2_opendir('%s') FAILED: nt_status=0x%08X (%s), error='%s'",
                    target_dir, (unsigned int)nt_err, nterror_to_str(nt_err), err);
        smb_log_nt_diagnostic(nt_err, clean_cfg.server, clean_cfg.share);
        if (out_err && err_sz > 0) {
            snprintf(out_err, err_sz, "Connected to share, but path '%s' not accessible: %s",
                     target_dir, err);
        }
        smb2_destroy_context(ctx);
        install_log("[SMB TEST] ==================== SMB CONNECTION TEST RESULT: FAILED ====================");
        return -1;
    }
    smb2_closedir(ctx, dir);
    install_log("[SMB TEST] Folder access verified: path '%s' is accessible", target_dir[0] ? target_dir : "/");

    /* Test write permissions if configured as read-write */
    if (!clean_cfg.is_read_only) {
        char test_file[512];
        if (target_dir[0] != '\0') {
            snprintf(test_file, sizeof(test_file), "%s/.pkgmgr_test", target_dir);
        } else {
            snprintf(test_file, sizeof(test_file), ".pkgmgr_test");
        }
        install_log("[SMB TEST] Testing write permission (creating test file '%s')...", test_file);

        struct smb2fh *tfh = smb2_open(ctx, test_file, O_WRONLY | O_CREAT | O_TRUNC);
        if (tfh) {
            smb2_close(ctx, tfh);
            smb2_unlink(ctx, test_file);
            install_log("[SMB TEST] Write permission confirmed (successfully created & removed test file)");
        } else {
            const char *err = smb2_get_error(ctx);
            if (!err || !*err) err = "Access denied";
            uint32_t nt_err = (uint32_t)smb2_get_nterror(ctx);
            install_log("[SMB TEST] Write permission DENIED: nt_status=0x%08X (%s), error='%s'. Share will be treated as Read-Only.",
                        (unsigned int)nt_err, nterror_to_str(nt_err), err);
            if (out_err && err_sz > 0) {
                snprintf(out_err, err_sz, "Connected, but share is read-only (%s)", err);
            }
            smb2_destroy_context(ctx);
            install_log("[SMB TEST] ==================== SMB CONNECTION TEST RESULT: SUCCESS (READ-ONLY) ====================");
            return 1; /* Note 1 = connected but read-only */
        }
    }

    smb2_destroy_context(ctx);
    install_log("[SMB TEST] Share mode: %s", clean_cfg.is_read_only ? "Read-Only" : "Read/Write");
    install_log("[SMB TEST] ==================== SMB CONNECTION TEST RESULT: SUCCESS ====================");
    if (out_err && err_sz > 0) {
        snprintf(out_err, err_sz, "Connected successfully (%s)", clean_cfg.is_read_only ? "Read-Only" : "Read/Write");
    }
    return 0;
}

/* Recursive directory scanner helper over SMB */
static int scan_smb_dir(struct smb2_context *ctx, const smb_share_config_t *cfg,
                        const char *sub_dir, int depth,
                        smb_pkg_callback_t pkg_cb, void *user_data) {
    if (depth > 4) return 0;

    const char *open_path = sub_dir;
    while (*open_path == '/') open_path++;

    struct smb2dir *dir = smb2_opendir(ctx, open_path);
    if (!dir) return (depth == 0) ? -1 : 0;

    int count = 0;
    struct smb2dirent *ent;
    while ((ent = smb2_readdir(ctx, dir)) != NULL) {
        if (ent->name[0] == '.') continue; /* skip hidden files, ., .., and dotfiles */

        char child_path[512];
        if (open_path[0] != '\0') {
            snprintf(child_path, sizeof(child_path), "%s/%s", open_path, ent->name);
        } else {
            snprintf(child_path, sizeof(child_path), "%s", ent->name);
        }

        if (ent->st.smb2_type == SMB2_TYPE_DIRECTORY) {
            /* Descend into subdirectories */
            int sub_count = scan_smb_dir(ctx, cfg, child_path, depth + 1, pkg_cb, user_data);
            if (sub_count > 0) count += sub_count;
        } else if (ent->st.smb2_type == SMB2_TYPE_FILE) {
            const char *name = ent->name;
            size_t nlen = strlen(name);
            int is_pkg = 0;
            if (nlen > 4 && strcasecmp(name + nlen - 4, ".pkg") == 0) {
                is_pkg = 1;
            }

            if (is_pkg) {
                char url[1024];
                if (cfg->port > 0 && cfg->port != SMB_DEFAULT_PORT) {
                    snprintf(url, sizeof(url), "smb://%s:%d/%s/%s", cfg->server, cfg->port, cfg->share, child_path);
                } else {
                    snprintf(url, sizeof(url), "smb://%s/%s/%s", cfg->server, cfg->share, child_path);
                }

                uint64_t fsz = (uint64_t)ent->st.smb2_size;
                uint32_t mtime = (uint32_t)ent->st.smb2_mtime;
                if (pkg_cb) {
                    pkg_cb(url, name, fsz, mtime, user_data);
                }
                count++;
            }
        }
    }

    smb2_closedir(ctx, dir);
    return count;
}

int smb_client_scan_share(const smb_share_config_t *cfg,
                          smb_pkg_callback_t pkg_cb,
                          void *user_data) {
    if (!cfg || !cfg->enabled) return 0;

    struct smb2_context *ctx = smb_connect(cfg, NULL, 0, 0);
    if (!ctx) return -1;

    const char *base_path = (cfg->path[0] != '\0' && strcmp(cfg->path, "/") != 0) ? cfg->path : "";
    int total = scan_smb_dir(ctx, cfg, base_path, 0, pkg_cb, user_data);

    smb2_destroy_context(ctx);
    return total;
}

int smb_client_count_pkg_files(const smb_share_config_t *cfg) {
    /* NULL callback => scan_smb_dir only readdirs and counts .pkg files,
       without any parsing or heavy I/O. */
    return smb_client_scan_share(cfg, NULL, NULL);
}

/* Opaque SMB file session implementation */
struct smb_file_session {
    struct smb2_context *ctx;
    struct smb2fh *fh;
    int local_fd;
    uint64_t file_size;
    char url[512];
    pthread_mutex_t mutex;
};

smb_file_session_t *smb_file_session_open(const char *smb_url) {
    if (!smb_url) return NULL;

    /* Local file path or file:// URL support for testing / fallback */
    const char *local_path = NULL;
    if (strncmp(smb_url, "file://", 7) == 0) {
        local_path = smb_url + 7;
    } else if (strncmp(smb_url, "smb://", 6) != 0) {
        local_path = smb_url;
    }

    if (local_path) {
        int lfd = open(local_path, O_RDONLY);
        if (lfd < 0) return NULL;

        struct stat st;
        if (fstat(lfd, &st) != 0) {
            close(lfd);
            return NULL;
        }

        smb_file_session_t *s = (smb_file_session_t *)calloc(1, sizeof(smb_file_session_t));
        if (!s) {
            close(lfd);
            return NULL;
        }

        s->local_fd = lfd;
        s->file_size = (uint64_t)st.st_size;
        strncpy(s->url, smb_url, sizeof(s->url) - 1);
        pthread_mutex_init(&s->mutex, NULL);
        return s;
    }

    smb_share_config_t cfg;
    if (smb_client_find_share_cfg(smb_url, &cfg) != 0) return NULL;

    char srv[128] = {0}, shr[128] = {0}, rel[256] = {0};
    int prt = SMB_DEFAULT_PORT;
    if (smb_client_parse_url(smb_url, srv, sizeof(srv), &prt, shr, sizeof(shr), rel, sizeof(rel)) != 0) {
        return NULL;
    }

    struct smb2_context *ctx = smb_connect(&cfg, NULL, 0, 0);
    if (!ctx) return NULL;

    const char *open_rel = rel;
    while (*open_rel == '/') open_rel++;

    struct smb2fh *fh = smb2_open(ctx, open_rel, O_RDONLY);
    if (!fh) {
        smb2_destroy_context(ctx);
        return NULL;
    }

    struct smb2_stat_64 st;
    uint64_t sz = 0;
    if (smb2_fstat(ctx, fh, &st) == 0) {
        sz = (uint64_t)st.smb2_size;
    }

    smb_file_session_t *s = (smb_file_session_t *)calloc(1, sizeof(smb_file_session_t));
    if (!s) {
        smb2_close(ctx, fh);
        smb2_destroy_context(ctx);
        return NULL;
    }

    s->ctx = ctx;
    s->fh = fh;
    s->file_size = sz;
    strncpy(s->url, smb_url, sizeof(s->url) - 1);
    pthread_mutex_init(&s->mutex, NULL);
    return s;
}

ssize_t smb_file_session_read(smb_file_session_t *session, void *buf, size_t count, uint64_t offset) {
    if (!session || !buf) return -1;

    pthread_mutex_lock(&session->mutex);

    if (session->local_fd > 0) {
        ssize_t n = pread(session->local_fd, buf, count, (off_t)offset);
        pthread_mutex_unlock(&session->mutex);
        return n;
    }

    if (!session->ctx || !session->fh) {
        pthread_mutex_unlock(&session->mutex);
        return -1;
    }

    size_t total_read = 0;
    uint8_t *p = (uint8_t *)buf;

    while (total_read < count) {
        size_t to_read = count - total_read;
        if (to_read > 64 * 1024) to_read = 64 * 1024;

        int rc = smb2_pread(session->ctx, session->fh, p + total_read, (uint32_t)to_read, offset + total_read);
        if (rc < 0) {
            pthread_mutex_unlock(&session->mutex);
            return (total_read > 0) ? (ssize_t)total_read : (ssize_t)rc;
        }
        if (rc == 0) {
            break; /* EOF */
        }
        total_read += (size_t)rc;
    }
    pthread_mutex_unlock(&session->mutex);

    return (ssize_t)total_read;
}

uint64_t smb_file_session_get_size(smb_file_session_t *session) {
    return session ? session->file_size : 0;
}

void smb_file_session_close(smb_file_session_t *session) {
    if (!session) return;
    pthread_mutex_lock(&session->mutex);
    if (session->local_fd > 0) {
        close(session->local_fd);
        session->local_fd = 0;
    }
    if (session->ctx && session->fh) {
        smb2_close(session->ctx, session->fh);
        session->fh = NULL;
    }
    if (session->ctx) {
        smb2_destroy_context(session->ctx);
        session->ctx = NULL;
    }
    pthread_mutex_unlock(&session->mutex);
    pthread_mutex_destroy(&session->mutex);
    free(session);
}

ssize_t smb_client_pread(const char *smb_url, void *buf, size_t count, uint64_t offset) {
    smb_file_session_t *s = smb_file_session_open(smb_url);
    if (!s) return -1;

    ssize_t n = smb_file_session_read(s, buf, count, offset);
    smb_file_session_close(s);
    return n;
}

int smb_client_stat(const char *smb_url, uint64_t *out_size, uint32_t *out_mtime) {
    if (!smb_url) return -1;

    smb_share_config_t cfg;
    if (smb_client_find_share_cfg(smb_url, &cfg) != 0) return -1;

    char srv[128] = {0}, shr[128] = {0}, rel[256] = {0};
    int prt = SMB_DEFAULT_PORT;
    if (smb_client_parse_url(smb_url, srv, sizeof(srv), &prt, shr, sizeof(shr), rel, sizeof(rel)) != 0) {
        return -1;
    }

    struct smb2_context *ctx = smb_connect(&cfg, NULL, 0, 0);
    if (!ctx) return -1;

    const char *open_rel = rel;
    while (*open_rel == '/') open_rel++;

    struct smb2_stat_64 st;
    int rc = smb2_stat(ctx, open_rel, &st);
    if (rc == 0) {
        if (out_size) *out_size = (uint64_t)st.smb2_size;
        if (out_mtime) *out_mtime = (uint32_t)st.smb2_mtime;
    }

    smb2_destroy_context(ctx);
    return rc;
}

int smb_client_calc_checksum(const char *smb_url, char *out_checksum, size_t out_max) {
    if (!smb_url || !out_checksum || out_max < 33) return -1;

    uint64_t file_size = 0;
    uint32_t mtime = 0;
    if (smb_client_stat(smb_url, &file_size, &mtime) != 0) {
        return -1;
    }

    uint8_t hdr[4096];
    ssize_t rd = smb_client_pread(smb_url, hdr, sizeof(hdr), 0);
    uint32_t crc = 0;
    if (rd > 0) {
        crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, hdr, (size_t)rd);
    }

    snprintf(out_checksum, out_max, "%08x%016llx%08x",
             (uint32_t)mtime,
             (unsigned long long)file_size,
             (uint32_t)crc);
    return 0;
}

/* Helper to extract a JSON string */
static int smb_json_extract_key(const char *json, size_t json_len, const char *key, char *out, size_t out_max) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = json;
    const char *end = json + json_len;

    while (p < end) {
        const char *found = strstr(p, pattern);
        if (!found || found >= end) return -1;

        const char *colon = strchr(found + strlen(pattern), ':');
        if (!colon || colon >= end) return -1;

        const char *q = colon + 1;
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;

        if (q < end && *q == '"') {
            q++;
            size_t idx = 0;
            while (q < end && *q != '"') {
                if (*q == '\\' && (q + 1) < end) q++;
                if (idx + 1 < out_max) out[idx++] = *q;
                q++;
            }
            out[idx] = '\0';
            return 0;
        }
        /* Unquoted scalar (true/false/null/numbers, as written by our own
           meta.json serializer): read until a structural delimiter. */
        if (q < end && *q != '{' && *q != '[') {
            size_t idx = 0;
            while (q < end && *q != ',' && *q != '}' && *q != ']' &&
                   *q != '\r' && *q != '\n') {
                if (*q == ' ' || *q == '\t') break;
                if (idx + 1 < out_max) out[idx++] = *q;
                q++;
            }
            while (idx > 0 && (out[idx - 1] == ' ' || out[idx - 1] == '\t')) idx--;
            out[idx] = '\0';
            return 0;
        }
        p = found + strlen(pattern);
    }
    return -1;
}

/* Parse PKG metadata directly from an SMB package using pread chunks */
int smb_client_parse_pkg(const char *smb_url, pkg_detail_t *out) {
    if (!smb_url || !out) return -1;

    memset(out, 0, sizeof(pkg_detail_t));
    strncpy(out->path, smb_url, sizeof(out->path) - 1);

    const char *slash = strrchr(smb_url, '/');
    if (slash && *(slash + 1) != '\0') {
        strncpy(out->filename, slash + 1, sizeof(out->filename) - 1);
    } else {
        strncpy(out->filename, smb_url, sizeof(out->filename) - 1);
    }

    smb_file_session_t *sess = smb_file_session_open(smb_url);
    if (!sess) return -1;

    out->file_size = sess->file_size;
    out->total_pkg_size = sess->file_size;

    uint8_t hdr[0x200];
    ssize_t hdr_read = smb_file_session_read(sess, hdr, sizeof(hdr), 0);
    if (hdr_read < 0x80) {
        smb_file_session_close(sess);
        return -1;
    }

    /* Multi-part archive format (PS5MPKG1) is only supported on local drives (USB / optical discs) */
    if (hdr_read >= MULTIPART_MAGIC_LEN && memcmp(hdr, MULTIPART_MAGIC, MULTIPART_MAGIC_LEN) == 0) {
        smb_file_session_close(sess);
        return -1;
    }

    uint64_t cnt_offset = 0;
    int cnt_found = 0;

    if (memcmp(hdr, "\x7f" "CNT", 4) == 0) {
        cnt_offset = 0;
        cnt_found = 1;
    } else if (memcmp(hdr, "\x7f" "FIH", 4) == 0) {
        uint64_t cand = smb_read_le64(hdr + 0x58);
        if (cand > 0 && cand < out->file_size) {
            uint8_t test_magic[4];
            if (smb_file_session_read(sess, test_magic, 4, cand) == 4 && memcmp(test_magic, "\x7f" "CNT", 4) == 0) {
                cnt_offset = cand;
                cnt_found = 1;
            }
        }
        if (!cnt_found) {
            for (size_t off = 0x10; off + 8 <= (size_t)hdr_read; off += 0x08) {
                cand = smb_read_le64(hdr + off);
                if (cand >= 0x10000 && cand < out->file_size && (cand % 0x1000) == 0) {
                    uint8_t test_magic[4];
                    if (smb_file_session_read(sess, test_magic, 4, cand) == 4 && memcmp(test_magic, "\x7f" "CNT", 4) == 0) {
                        cnt_offset = cand;
                        cnt_found = 1;
                        break;
                    }
                }
            }
        }
    }

    if (!cnt_found) {
        smb_file_session_close(sess);
        return -1;
    }

    uint8_t cnt_hdr[0x80];
    if (smb_file_session_read(sess, cnt_hdr, sizeof(cnt_hdr), cnt_offset) != sizeof(cnt_hdr) ||
        memcmp(cnt_hdr, "\x7f" "CNT", 4) != 0) {
        smb_file_session_close(sess);
        return -1;
    }

    uint32_t cnt_type_magic = smb_read_be32(cnt_hdr + 0x04);
    memcpy(out->content_id, cnt_hdr + 0x40, 48);
    out->content_id[48] = '\0';
    for (int i = 0; i < 48; i++) {
        if ((unsigned char)out->content_id[i] < 32 || (unsigned char)out->content_id[i] > 126) {
            out->content_id[i] = '\0';
            break;
        }
    }

    uint32_t entry_count = smb_read_be32(cnt_hdr + 0x10);
    uint32_t table_offset = smb_read_be32(cnt_hdr + 0x18);
    if (entry_count == 0 || entry_count > 2048 || table_offset > 0x200000) {
        smb_file_session_close(sess);
        return -1;
    }

    size_t table_size = entry_count * 32;
    uint8_t *entry_table = (uint8_t *)malloc(table_size);
    if (!entry_table) {
        smb_file_session_close(sess);
        return -1;
    }

    if (smb_file_session_read(sess, entry_table, table_size, cnt_offset + table_offset) != (ssize_t)table_size) {
        free(entry_table);
        smb_file_session_close(sess);
        return -1;
    }

    uint32_t str_table_off = 0;
    uint32_t str_table_sz = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        const uint8_t *e = entry_table + i * 32;
        uint32_t type = smb_read_be32(e);
        if (type == 0x0200) {
            str_table_off = smb_read_be32(e + 16);
            str_table_sz = smb_read_be32(e + 20);
            break;
        }
    }

    char *str_table = NULL;
    if (str_table_sz > 0 && str_table_sz < 65536) {
        str_table = (char *)malloc(str_table_sz + 1);
        if (str_table) {
            if (smb_file_session_read(sess, str_table, str_table_sz, cnt_offset + str_table_off) == (ssize_t)str_table_sz) {
                str_table[str_table_sz] = '\0';
            } else {
                free(str_table);
                str_table = NULL;
            }
        }
    }

    int has_playgo_chunk_patch = 0;
    int has_delta_patch = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        const uint8_t *e = entry_table + i * 32;
        uint32_t type = smb_read_be32(e);
        uint32_t fn_off = smb_read_be32(e + 4);
        uint32_t data_off = smb_read_be32(e + 16);
        uint32_t data_sz = smb_read_be32(e + 20);

        const char *name = "";
        if (str_table && fn_off < str_table_sz) {
            name = str_table + fn_off;
        }

        if (type == 0x1008 || strcmp(name, "app/playgo-chunk.dat") == 0) {
            has_playgo_chunk_patch = 1;
        }
        if (type == 0x0407 || type == 0x0408 ||
            strcmp(name, "target-deltainfo.dat") == 0 || strcmp(name, "origin-deltainfo.dat") == 0) {
            has_delta_patch = 1;
        }

        /* 1. param.json */
        if ((type == 0x2000 || strcmp(name, "param.json") == 0) && data_sz > 0 && data_sz < 262144) {
            char *json_buf = (char *)malloc(data_sz + 1);
            if (json_buf) {
                if (smb_file_session_read(sess, json_buf, data_sz, cnt_offset + data_off) == (ssize_t)data_sz) {
                    json_buf[data_sz] = '\0';
                    char tid[PKG_TITLE_ID_LEN] = {0};
                    char tname[PKG_TITLE_NAME_LEN] = {0};
                    if (smb_json_extract_key(json_buf, data_sz, "titleId", tid, sizeof(tid)) == 0) {
                        strncpy(out->title_id, tid, sizeof(out->title_id) - 1);
                    }
                    if (smb_json_extract_key(json_buf, data_sz, "titleName", tname, sizeof(tname)) == 0) {
                        strncpy(out->title_name, tname, sizeof(out->title_name) - 1);
                    }

                    char cat_buf[16] = {0};
                    if (smb_json_extract_key(json_buf, data_sz, "category", cat_buf, sizeof(cat_buf)) == 0 && out->category[0] == '\0') {
                        strncpy(out->category, cat_buf, sizeof(out->category) - 1);
                    }
                    char ver[32] = {0};
                    if (smb_json_extract_key(json_buf, data_sz, "contentVersion", ver, sizeof(ver)) == 0 ||
                        smb_json_extract_key(json_buf, data_sz, "appVersion", ver, sizeof(ver)) == 0 ||
                        smb_json_extract_key(json_buf, data_sz, "version", ver, sizeof(ver)) == 0) {
                        if (ver[0] != '\0') {
                            int maj = 0, min = 0, patch = 0;
                            if (sscanf(ver, "%d.%d.%d", &maj, &min, &patch) == 3) {
                                if (min == 0 && patch > 0) {
                                    snprintf(out->app_version, sizeof(out->app_version), "v%d.%02d", maj, patch);
                                } else if (patch == 0) {
                                    snprintf(out->app_version, sizeof(out->app_version), "v%d.%02d", maj, min);
                                } else {
                                    snprintf(out->app_version, sizeof(out->app_version), "v%d.%d.%d", maj, min, patch);
                                }
                            } else if (ver[0] != 'v' && ver[0] != 'V') {
                                snprintf(out->app_version, sizeof(out->app_version), "v%.29s", ver);
                            } else {
                                strncpy(out->app_version, ver, sizeof(out->app_version) - 1);
                            }
                        }
                    }
                }
                free(json_buf);
            }
        }

        /* 2. param.sfo */
        if ((type == 0x1000 || strcmp(name, "param.sfo") == 0) && data_sz > 0 && data_sz < 262144) {
            uint8_t *sfo_buf = (uint8_t *)malloc(data_sz);
            if (sfo_buf) {
                if (smb_file_session_read(sess, sfo_buf, data_sz, cnt_offset + data_off) == (ssize_t)data_sz) {
                    if (data_sz >= 20 && memcmp(sfo_buf, "\x00PSF", 4) == 0) {
                        uint32_t key_tbl_off = smb_read_le32(sfo_buf + 0x08);
                        uint32_t val_tbl_off = smb_read_le32(sfo_buf + 0x0C);
                        uint32_t entry_cnt = smb_read_le32(sfo_buf + 0x10);

                        char sfo_app_ver[32] = {0};
                        char sfo_version[32] = {0};

                        for (uint32_t j = 0; j < entry_cnt && (0x14 + j * 16) + 16 <= data_sz; j++) {
                            const uint8_t *se = sfo_buf + 0x14 + j * 16;
                            uint16_t k_off = smb_read_le16(se);
                            uint32_t v_len = smb_read_le32(se + 0x04);
                            uint32_t v_off = smb_read_le32(se + 0x0C);

                            if (key_tbl_off + k_off < data_sz && val_tbl_off + v_off + v_len <= data_sz) {
                                const char *k = (const char *)(sfo_buf + key_tbl_off + k_off);
                                const char *v = (const char *)(sfo_buf + val_tbl_off + v_off);

                                if (strcmp(k, "TITLE_ID") == 0 && out->title_id[0] == '\0') {
                                    size_t l = v_len < sizeof(out->title_id) ? v_len : sizeof(out->title_id) - 1;
                                    while (l > 0 && v[l - 1] == '\0') l--;
                                    strncpy(out->title_id, v, l);
                                    out->title_id[l] = '\0';
                                } else if (strcmp(k, "TITLE") == 0 && out->title_name[0] == '\0') {
                                    size_t l = v_len < sizeof(out->title_name) ? v_len : sizeof(out->title_name) - 1;
                                    while (l > 0 && v[l - 1] == '\0') l--;
                                    strncpy(out->title_name, v, l);
                                    out->title_name[l] = '\0';
                                } else if (strcmp(k, "APP_VER") == 0 && sfo_app_ver[0] == '\0') {
                                    size_t l = v_len < sizeof(sfo_app_ver) ? v_len : sizeof(sfo_app_ver) - 1;
                                    while (l > 0 && v[l - 1] == '\0') l--;
                                    strncpy(sfo_app_ver, v, l);
                                    sfo_app_ver[l] = '\0';
                                } else if (strcmp(k, "VERSION") == 0 && sfo_version[0] == '\0') {
                                    size_t l = v_len < sizeof(sfo_version) ? v_len : sizeof(sfo_version) - 1;
                                    while (l > 0 && v[l - 1] == '\0') l--;
                                    strncpy(sfo_version, v, l);
                                    sfo_version[l] = '\0';
                                } else if (strcmp(k, "CATEGORY") == 0 && out->category[0] == '\0') {
                                    size_t l = v_len < sizeof(out->category) ? v_len : sizeof(out->category) - 1;
                                    while (l > 0 && v[l - 1] == '\0') l--;
                                    strncpy(out->category, v, l);
                                    out->category[l] = '\0';
                                }
                            }
                        }

                        const char *best_v = (sfo_app_ver[0] != '\0') ? sfo_app_ver : sfo_version;
                        if (best_v && best_v[0] != '\0' && out->app_version[0] == '\0') {
                            if (best_v[0] != 'v' && best_v[0] != 'V') {
                                snprintf(out->app_version, sizeof(out->app_version), "v%.29s", best_v);
                            } else {
                                strncpy(out->app_version, best_v, sizeof(out->app_version) - 1);
                                out->app_version[sizeof(out->app_version) - 1] = '\0';
                            }
                        }
                    }
                }
                free(sfo_buf);
            }
        }

        /* 3. icon0.png */
        if ((type == 0x1200 || strcmp(name, "icon0.png") == 0) && data_sz > 0) {
            out->has_icon = 1;
            out->icon_offset = cnt_offset + data_off;
            out->icon_size = data_sz;
        }
    }

    int is_delta_type = ((cnt_type_magic & 0xFF) == 0x1E || (cnt_type_magic & 0xFF000000) == 0x41000000);

    if (has_playgo_chunk_patch || has_delta_patch || is_delta_type ||
        (out->category[0] != '\0' && strncmp(out->category, "gp", 2) == 0)) {
        out->pkg_type = PKG_TYPE_UPDATE;
    } else if (out->category[0] != '\0') {
        if (strncmp(out->category, "ac", 2) == 0 || strncmp(out->category, "al", 2) == 0 ||
            strcmp(out->category, "addcont") == 0) {
            out->pkg_type = PKG_TYPE_DLC;
        } else if (strncmp(out->category, "gd", 2) == 0 || strncmp(out->category, "bd", 2) == 0 ||
                   strncmp(out->category, "gc", 2) == 0 || strncmp(out->category, "wt", 2) == 0) {
            out->pkg_type = PKG_TYPE_BASE;
        }
    } else if (cnt_type_magic == 1) {
        out->pkg_type = PKG_TYPE_DLC;
    }

    if (out->pkg_type == PKG_TYPE_UNKNOWN) {
        out->pkg_type = PKG_TYPE_BASE;
    }

    switch (out->pkg_type) {
        case PKG_TYPE_BASE:
            strncpy(out->pkg_type_str, "base", sizeof(out->pkg_type_str) - 1);
            break;
        case PKG_TYPE_UPDATE:
            strncpy(out->pkg_type_str, "update", sizeof(out->pkg_type_str) - 1);
            break;
        case PKG_TYPE_DLC:
            strncpy(out->pkg_type_str, "dlc", sizeof(out->pkg_type_str) - 1);
            break;
        default:
            strncpy(out->pkg_type_str, "unknown", sizeof(out->pkg_type_str) - 1);
            break;
    }

    if (str_table) free(str_table);
    free(entry_table);
    smb_file_session_close(sess);

    /* Fallback if title_id could not be found from param.sfo / param.json */
    if (out->title_id[0] == '\0' && out->content_id[0] != '\0') {
        const char *dash = strchr(out->content_id, '-');
        if (dash) {
            const char *us = strchr(dash + 1, '_');
            if (us && (size_t)(us - (dash + 1)) < sizeof(out->title_id)) {
                size_t len = us - (dash + 1);
                strncpy(out->title_id, dash + 1, len);
                out->title_id[len] = '\0';
            }
        }
    }

    if (out->title_name[0] == '\0') {
        if (out->title_id[0] != '\0') {
            snprintf(out->title_name, sizeof(out->title_name), "%s", out->title_id);
        } else {
            snprintf(out->title_name, sizeof(out->title_name), "Unknown Package");
        }
    }

    out->is_valid = 1;
    return 0;
}

/* Direct icon retrieval from SMB PKG */
int smb_client_get_icon(const char *smb_url, uint8_t **out_data, size_t *out_size) {
    if (!smb_url || !out_data || !out_size) return -1;

    /* Parse PKG header to get icon_offset and icon_size */
    pkg_detail_t detail;
    if (smb_client_parse_pkg(smb_url, &detail) == 0 && detail.has_icon && detail.icon_size > 0) {
        uint8_t *buf = (uint8_t *)malloc(detail.icon_size);
        if (!buf) return -1;

        ssize_t n = smb_client_pread(smb_url, buf, detail.icon_size, detail.icon_offset);
        if (n == (ssize_t)detail.icon_size) {
            *out_data = buf;
            *out_size = detail.icon_size;
            return 0;
        }
        free(buf);
    }

    return -1;
}
