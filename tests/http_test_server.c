/*
 * Tiny threaded HTTP/1.1 file server for host tests (see http_test_server.h).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* strcasestr on glibc */
#endif
#include "http_test_server.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define TS_SEND_FLAGS MSG_NOSIGNAL
#else
#define TS_SEND_FLAGS 0
#endif

static struct {
    int listen_fd;
    volatile int running;
    pthread_t thread;
    char root[512];
    char auth_b64[256];
    int no_range;
    int json_listing;
    int gateway;
    volatile int sign_ttl;
    int port;
    pthread_mutex_t stats_lock;
    http_test_server_stats_t stats;
} g_ts = { -1, 0, 0, "", "", 0, 0, 0, 3600, 0, PTHREAD_MUTEX_INITIALIZER, { 0, 0, 0, 0 } };

#define TS_STAT(field) do { pthread_mutex_lock(&g_ts.stats_lock); g_ts.stats.field++; \
                            pthread_mutex_unlock(&g_ts.stats_lock); } while (0)

void http_test_server_get_stats(http_test_server_stats_t *out) {
    pthread_mutex_lock(&g_ts.stats_lock);
    *out = g_ts.stats;
    pthread_mutex_unlock(&g_ts.stats_lock);
}

void http_test_server_set_sign_ttl(int seconds) {
    g_ts.sign_ttl = seconds;
}

static void ts_b64(const char *in, char *out, size_t out_sz) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = strlen(in), o = 0;
    for (size_t i = 0; i < n && o + 5 < out_sz; i += 3) {
        unsigned v = (unsigned char)in[i] << 16;
        if (i + 1 < n) v |= (unsigned char)in[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned char)in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? tbl[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? tbl[v & 63] : '=';
    }
    out[o] = '\0';
}

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, TS_SEND_FLAGS);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static void url_decode(const char *in, char *out, size_t out_sz) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_sz; i++) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            char h[3] = { in[i + 1], in[i + 2], 0 };
            out[o++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

static void url_encode(const char *in, char *out, size_t out_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 4 < out_sz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

static int send_simple(int fd, int code, const char *reason, const char *extra, int keep) {
    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr), "HTTP/1.1 %d %s\r\nContent-Length: 0\r\n%sConnection: %s\r\n\r\n",
                     code, reason, extra ? extra : "", keep ? "keep-alive" : "close");
    return send_all(fd, hdr, (size_t)n);
}

/* nginx "autoindex_format json" */
static int send_json_listing(int fd, const char *fs_path, int head, int keep) {
    DIR *d = opendir(fs_path);
    if (!d) return send_simple(fd, 404, "Not Found", NULL, keep);
    size_t cap = 65536, len = 0;
    char *body = (char *)malloc(cap);
    if (!body) {
        closedir(d);
        return -1;
    }
    len += (size_t)snprintf(body + len, cap - len, "[\n");
    int first = 1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", fs_path, e->d_name);
        struct stat st;
        if (stat(child, &st) != 0 || len + 2048 > cap) continue;
        char date[64];
        struct tm tmv;
        time_t mt = st.st_mtime;
        gmtime_r(&mt, &tmv);
        strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &tmv);
        if (S_ISDIR(st.st_mode)) {
            len += (size_t)snprintf(body + len, cap - len,
                                    "%s{ \"name\":\"%s\", \"type\":\"directory\", \"mtime\":\"%s\" }\n",
                                    first ? "" : ",", e->d_name, date);
        } else {
            len += (size_t)snprintf(body + len, cap - len,
                                    "%s{ \"name\":\"%s\", \"type\":\"file\", \"mtime\":\"%s\", \"size\":%llu }\n",
                                    first ? "" : ",", e->d_name, date, (unsigned long long)st.st_size);
        }
        first = 0;
    }
    closedir(d);
    len += (size_t)snprintf(body + len, cap - len, "]\n");
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: %s\r\n\r\n",
                     len, keep ? "keep-alive" : "close");
    int rc = send_all(fd, hdr, (size_t)n);
    if (rc == 0 && !head) rc = send_all(fd, body, len);
    free(body);
    return rc;
}

static int send_listing(int fd, const char *fs_path, const char *url_path, int head, int keep) {
    if (g_ts.json_listing) return send_json_listing(fd, fs_path, head, keep);
    DIR *d = opendir(fs_path);
    if (!d) return send_simple(fd, 404, "Not Found", NULL, keep);
    size_t cap = 65536, len = 0;
    char *body = (char *)malloc(cap);
    if (!body) {
        closedir(d);
        return -1;
    }
    len += (size_t)snprintf(body + len, cap - len,
                            "<html><head><title>Index of %s</title></head><body>\n"
                            "<a href=\"?C=N;O=D\">Name</a> <img src=\"/icons/blank.gif\">\n"
                            "<a href=\"/icons/back.gif\">icon</a>\n"
                            "<a href=\"../\">Parent Directory</a>\n",
                            url_path);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char child[1024], enc[768];
        snprintf(child, sizeof(child), "%s/%s", fs_path, e->d_name);
        struct stat st;
        if (stat(child, &st) != 0) continue;
        url_encode(e->d_name, enc, sizeof(enc));
        if (len + 2048 > cap) break;
        len += (size_t)snprintf(body + len, cap - len, "<a href=\"%s%s\">%s%s</a>\n",
                                enc, S_ISDIR(st.st_mode) ? "/" : "", e->d_name,
                                S_ISDIR(st.st_mode) ? "/" : "");
    }
    closedir(d);
    len += (size_t)snprintf(body + len, cap - len, "</body></html>\n");

    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\nConnection: %s\r\n\r\n",
                     len, keep ? "keep-alive" : "close");
    int rc = send_all(fd, hdr, (size_t)n);
    if (rc == 0 && !head) rc = send_all(fd, body, len);
    free(body);
    return rc;
}

static int send_file(int fd, const char *fs_path, const struct stat *st, const char *range,
                     int head, int keep) {
    uint64_t size = (uint64_t)st->st_size;
    uint64_t start = 0, end = size ? size - 1 : 0;
    int partial = 0;
    if (range && !g_ts.no_range) {
        unsigned long long a = 0, b = 0;
        int got = sscanf(range, "bytes=%llu-%llu", &a, &b);
        if (got >= 1) {
            if (a >= size) {
                char extra[96];
                snprintf(extra, sizeof(extra), "Content-Range: bytes */%llu\r\n", (unsigned long long)size);
                return send_simple(fd, 416, "Range Not Satisfiable", extra, keep);
            }
            start = a;
            end = (got == 2 && b < size) ? b : size - 1;
            partial = 1;
        }
    }

    char date[64];
    struct tm tmv;
    time_t mt = st->st_mtime;
    gmtime_r(&mt, &tmv);
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &tmv);

    char hdr[512];
    int n;
    uint64_t clen = size ? end - start + 1 : 0;
    if (partial) {
        n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 206 Partial Content\r\nContent-Length: %llu\r\n"
                     "Content-Range: bytes %llu-%llu/%llu\r\nAccept-Ranges: bytes\r\n"
                     "Last-Modified: %s\r\nConnection: %s\r\n\r\n",
                     (unsigned long long)clen, (unsigned long long)start, (unsigned long long)end,
                     (unsigned long long)size, date, keep ? "keep-alive" : "close");
    } else {
        n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\nContent-Length: %llu\r\n%sLast-Modified: %s\r\nConnection: %s\r\n\r\n",
                     (unsigned long long)clen, g_ts.no_range ? "" : "Accept-Ranges: bytes\r\n",
                     date, keep ? "keep-alive" : "close");
    }
    if (send_all(fd, hdr, (size_t)n) != 0) return -1;
    if (head || clen == 0) return 0;

    int ffd = open(fs_path, O_RDONLY);
    if (ffd < 0) return -1;
    char buf[65536];
    uint64_t off = start, left = clen;
    int rc = 0;
    while (left > 0) {
        size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
        ssize_t r = pread(ffd, buf, want, (off_t)off);
        if (r <= 0 || send_all(fd, buf, (size_t)r) != 0) {
            rc = -1;
            break;
        }
        off += (uint64_t)r;
        left -= (uint64_t)r;
    }
    close(ffd);
    return rc;
}

static void *conn_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char req[8192];
    for (;;) {
        size_t len = 0;
        req[0] = '\0';
        while (!strstr(req, "\r\n\r\n") && len + 1 < sizeof(req)) {
            ssize_t n = recv(fd, req + len, sizeof(req) - 1 - len, 0);
            if (n <= 0) goto done;
            len += (size_t)n;
            req[len] = '\0';
        }
        char method[16] = "", raw_path[2048] = "";
        if (sscanf(req, "%15s %2047s", method, raw_path) != 2) goto done;
        int head = strcmp(method, "HEAD") == 0;

        const char *range = NULL, *auth = NULL;
        int keep = 1;
        char range_buf[128] = "", auth_buf[512] = "";
        for (char *line = strstr(req, "\r\n"); line && line[2]; line = strstr(line + 2, "\r\n")) {
            char *h = line + 2;
            if (strncasecmp(h, "Range:", 6) == 0) {
                sscanf(h + 6, " %127[^\r\n]", range_buf);
                range = range_buf;
            } else if (strncasecmp(h, "Authorization:", 14) == 0) {
                sscanf(h + 14, " Basic %511[^\r\n]", auth_buf);
                auth = auth_buf;
            } else if (strncasecmp(h, "Connection:", 11) == 0 && strcasestr(h, "close")) {
                keep = 0;
            }
        }

        /* Gateway: signed "R2" links need no credentials and expire. */
        if (g_ts.gateway && strncmp(raw_path, "/signed/", 8) == 0) {
            TS_STAT(signed_hits);
            if (auth) TS_STAT(leaked_auth);
            char *rest = raw_path + 8;
            long expiry = strtol(rest, &rest, 10);
            if (*rest != '/' || (long)time(NULL) > expiry) {
                TS_STAT(signed_expired);
                if (send_simple(fd, 403, "Forbidden", NULL, keep) != 0 || !keep) goto done;
                continue;
            }
            memmove(raw_path, rest, strlen(rest) + 1); /* serve /<path> below */
            goto serve;
        }

        if (g_ts.auth_b64[0] && (!auth || strcmp(auth, g_ts.auth_b64) != 0)) {
            if (send_simple(fd, 401, "Unauthorized", "WWW-Authenticate: Basic realm=\"pkgs\"\r\n", keep) != 0 || !keep) goto done;
            continue;
        }

        char *q = strchr(raw_path, '?');
        if (q) *q = '\0';

        if (g_ts.gateway && strncmp(raw_path, "/files/", 7) == 0) {
            TS_STAT(files_hits);
            char dec[2048], fsp[2600];
            url_decode(raw_path + 6, dec, sizeof(dec));
            snprintf(fsp, sizeof(fsp), "%s%s", g_ts.root, dec);
            struct stat gst;
            int rc;
            if (strstr(dec, "..") || stat(fsp, &gst) != 0 || !S_ISREG(gst.st_mode)) {
                rc = send_simple(fd, 404, "Not Found", NULL, keep);
            } else {
                char extra[2400];
                snprintf(extra, sizeof(extra), "Location: http://localhost:%d/signed/%ld%s\r\n",
                         g_ts.port, (long)time(NULL) + g_ts.sign_ttl, raw_path + 6);
                rc = send_simple(fd, 302, "Found", extra, keep);
            }
            if (rc != 0 || !keep) goto done;
            continue;
        }

        if (strncmp(raw_path, "/redir/", 7) == 0) {
            char extra[2200];
            snprintf(extra, sizeof(extra), "Location: /%s\r\n", raw_path + 7);
            if (send_simple(fd, 302, "Found", extra, keep) != 0 || !keep) goto done;
            continue;
        }

    serve:;
        char path[2048];
        url_decode(raw_path, path, sizeof(path));
        if (strstr(path, "..")) {
            if (send_simple(fd, 403, "Forbidden", NULL, keep) != 0 || !keep) goto done;
            continue;
        }
        char fs_path[2600];
        snprintf(fs_path, sizeof(fs_path), "%s%s", g_ts.root, path);
        size_t fl = strlen(fs_path);
        while (fl > 1 && fs_path[fl - 1] == '/') fs_path[--fl] = '\0';

        struct stat st;
        int rc;
        if (stat(fs_path, &st) != 0) {
            rc = send_simple(fd, 404, "Not Found", NULL, keep);
        } else if (S_ISDIR(st.st_mode)) {
            if (raw_path[strlen(raw_path) - 1] != '/') {
                char extra[2200];
                snprintf(extra, sizeof(extra), "Location: %s/\r\n", raw_path);
                rc = send_simple(fd, 301, "Moved Permanently", extra, keep);
            } else {
                rc = send_listing(fd, fs_path, raw_path, head, keep);
            }
        } else {
            rc = send_file(fd, fs_path, &st, range, head, keep);
        }
        if (rc != 0 || !keep) goto done;
    }
done:
    close(fd);
    return NULL;
}

static void *accept_thread(void *arg) {
    (void)arg;
    while (g_ts.running) {
        int c = accept(g_ts.listen_fd, NULL, NULL);
        if (c < 0) {
            if (!g_ts.running) break;
            continue;
        }
        pthread_t t;
        if (pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)c) == 0) {
            pthread_detach(t);
        } else {
            close(c);
        }
    }
    return NULL;
}

int http_test_server_start(http_test_server_opts_t *o) {
    snprintf(g_ts.root, sizeof(g_ts.root), "%s", o->root);
    g_ts.no_range = o->no_range;
    g_ts.json_listing = o->json_listing;
    g_ts.gateway = o->gateway;
    g_ts.sign_ttl = o->sign_ttl > 0 ? o->sign_ttl : 3600;
    pthread_mutex_lock(&g_ts.stats_lock);
    memset(&g_ts.stats, 0, sizeof(g_ts.stats));
    pthread_mutex_unlock(&g_ts.stats_lock);
    g_ts.auth_b64[0] = '\0';
    if (o->user) {
        char creds[256];
        snprintf(creds, sizeof(creds), "%s:%s", o->user, o->pass ? o->pass : "");
        ts_b64(creds, g_ts.auth_b64, sizeof(g_ts.auth_b64));
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 32) != 0) {
        close(fd);
        return -1;
    }
    socklen_t al = sizeof(a);
    getsockname(fd, (struct sockaddr *)&a, &al);
    o->port = ntohs(a.sin_port);
    g_ts.port = o->port;

    g_ts.listen_fd = fd;
    g_ts.running = 1;
    if (pthread_create(&g_ts.thread, NULL, accept_thread, NULL) != 0) {
        g_ts.running = 0;
        close(fd);
        return -1;
    }
    return 0;
}

void http_test_server_stop(void) {
    if (!g_ts.running) return;
    g_ts.running = 0;
    shutdown(g_ts.listen_fd, SHUT_RDWR);
    close(g_ts.listen_fd);
    pthread_join(g_ts.thread, NULL);
    g_ts.listen_fd = -1;
}
