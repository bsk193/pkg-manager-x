/*
 * PKG Manager X - PS4 home screen tile.
 *
 * PS4 has no "web link" tile like the PS5 deeplink shortcut, so this tiny
 * app does the same job, and only that:
 *   1. If PKG Manager X is not running (nothing on 127.0.0.1:8844), send the
 *      bundled payload (/app0/pkgmgr-ps4.elf) to GoldHEN's BinLoader on
 *      127.0.0.1:9090 and wait for the web server to come up.
 *   2. Show http://127.0.0.1:8844/ in the system web browser dialog, inside
 *      this app. Unlike the Browser app it opens no new browser window per
 *      launch and leaves the Browser app's windows alone. Circle closes
 *      the dialog. If the dialog cannot open, the Browser app is used.
 *   3. Close itself (sceSystemServiceLoadExec("exit")), back to the home
 *      screen. The tile never stops or restarts the PKG Manager X service:
 *      that runs until reboot / rest mode. (The payload also refuses to
 *      replace a running copy of the same version.)
 * Optional: a /app0/preset_sources.json (one HTTP source object, e.g. a
 * private build with a home server) is added through the local API once the
 * server is up, unless a source with the same URL already exists.
 * PS4 apps must not return from main: the system reports that as a crash
 * (CE-34878-0).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <orbis/CommonDialog.h>
#include <orbis/Sysmodule.h>

#define PKGMGR_PORT     8844
#define BINLOADER_PORT  9090
#define PAYLOAD_PATH    "/app0/pkgmgr-ps4.elf"
#define UI_URL          "http://127.0.0.1:8844/"
#define START_WAIT_SEC  25
#define PRESET_PATH     "/app0/preset_sources.json"

int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);
int sceSystemServiceLaunchWebBrowser(const char *uri, void *param);
int sceSystemServiceHideSplashScreen(void);
int sceKernelUsleep(unsigned int usec);
int sceSystemServiceLoadExec(const char *path, const char *args[]);
int sceUserServiceGetForegroundUser(int *user_id);
int sceUserServiceGetInitialUser(int *user_id);

/* libSceWebBrowserDialog has no OpenOrbis types; this is Sony's
 * SceWebBrowserDialogParam as used by RommPS, Nuvio-PS5 and EVO Player. */
typedef struct {
    OrbisCommonDialogBaseParam base;
    uint64_t size;
    int32_t mode; /* 1: the browser's own layout, 2: the rectangle below */
    int32_t user_id;
    const char *url;
    void *callback_init;
    uint16_t width, height, pos_x, pos_y;
    uint32_t parts;
    uint16_t header_width, header_x, header_y, pad0;
    uint32_t control;
    void *ime_param;
    void *webview_param;
    uint32_t animation;
    uint8_t reserved[202];
    uint16_t tail_pad;
} web_dialog_param_t;
_Static_assert(sizeof(web_dialog_param_t) == 328, "SceWebBrowserDialogParam is 328 bytes");

int sceWebBrowserDialogInitialize(void);
int sceWebBrowserDialogOpen(web_dialog_param_t *param);
int sceWebBrowserDialogUpdateStatus(void);
int sceWebBrowserDialogClose(void);
int sceWebBrowserDialogTerminate(void);

/* libkernel notification (same layout as OpenOrbis' OrbisNotificationRequest). */
typedef struct {
    int type;
    int reqId;
    int priority;
    int msgId;
    int targetId;
    int userId;
    int unk1;
    int unk2;
    int appId;
    int errorNum;
    int unk3;
    unsigned char useIconImageUri;
    char message[1024];
    char iconUri[1024];
    char unk[1024];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int blocking);

static void notify(const char *fmt, ...) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, ap);
    va_end(ap);
    req.type = 0;
    req.targetId = -1;
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static int local_connect(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int port_open(int port) {
    int fd = local_connect(port);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

/* Sends the bundled payload to GoldHEN's BinLoader. 0 on success. */
static int send_payload(void) {
    int in = open(PAYLOAD_PATH, O_RDONLY);
    if (in < 0) return -2;
    int out = local_connect(BINLOADER_PORT);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[64 * 1024];
    int rc = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n < 0) { rc = -3; break; }
        if (n == 0) break;
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w <= 0) { rc = -3; break; }
            off += w;
        }
        if (rc) break;
    }
    close(out);
    close(in);
    return rc;
}

/* ── Optional preset HTTP source ─────────────────────────────────────── */

/* Reads a whole file (NUL-terminated), NULL if missing. */
static char *read_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 64 * 1024) {
        close(fd);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)st.st_size + 1);
    ssize_t n = buf ? read(fd, buf, (size_t)st.st_size) : -1;
    close(fd);
    if (n != st.st_size) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

/* One HTTP/1.0 request to the local server; returns the body (malloc) or NULL. */
static char *local_request(const char *method, const char *path, const char *body) {
    int fd = local_connect(PKGMGR_PORT);
    if (fd < 0) return NULL;
    char hdr[256];
    size_t blen = body ? strlen(body) : 0;
    int hl = snprintf(hdr, sizeof(hdr),
                      "%s %s HTTP/1.0\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
                      "Content-Length: %u\r\nConnection: close\r\n\r\n",
                      method, path, (unsigned)blen);
    if (write(fd, hdr, (size_t)hl) != hl || (blen && write(fd, body, blen) != (ssize_t)blen)) {
        close(fd);
        return NULL;
    }
    size_t cap = 16384, len = 0;
    char *resp = (char *)malloc(cap);
    for (;;) {
        if (!resp) break;
        if (len + 4096 + 1 > cap) {
            char *n2 = (char *)realloc(resp, cap * 2);
            if (!n2) { free(resp); resp = NULL; break; }
            resp = n2;
            cap *= 2;
        }
        ssize_t n = read(fd, resp + len, 4096);
        if (n <= 0) break;
        len += (size_t)n;
    }
    close(fd);
    if (!resp) return NULL;
    resp[len] = '\0';
    char *b = strstr(resp, "\r\n\r\n");
    if (strncmp(resp, "HTTP/1.", 7) != 0 || !b || atoi(resp + 9) != 200) {
        free(resp);
        return NULL;
    }
    memmove(resp, b + 4, strlen(b + 4) + 1);
    return resp;
}

/* Copies the "url" value of a JSON object, normalized with a trailing '/'. */
static void preset_url(const char *obj, char *out, size_t out_sz) {
    out[0] = '\0';
    const char *k = strstr(obj, "\"url\"");
    if (!k) return;
    k = strchr(k + 5, '"');
    if (!k) return;
    k++;
    size_t o = 0;
    while (*k && *k != '"' && o + 2 < out_sz) out[o++] = *k++;
    if (o && out[o - 1] != '/') out[o++] = '/';
    out[o] = '\0';
}

/* Adds the bundled preset source once, unless its URL is already configured. */
static void apply_preset_source(void) {
    char *preset = read_file(PRESET_PATH);
    if (!preset) return;
    char url[600];
    preset_url(preset, url, sizeof(url));
    char *list = url[0] ? local_request("GET", "/api/http/sources", NULL) : NULL;
    if (list) {
        char needle[640];
        snprintf(needle, sizeof(needle), "\"url\":\"%s\"", url);
        if (!strstr(list, needle)) {
            /* Existing entries come back with blank passwords, which the
             * server keeps as they are. */
            char *close_br = strrchr(list, ']');
            char *obj = strchr(preset, '{');
            char *obj_end = strrchr(preset, '}');
            if (close_br && obj && obj_end && obj_end > obj) {
                *close_br = '\0';
                *(obj_end + 1) = '\0';
                int empty = strchr(list, '{') == NULL;
                size_t sz = strlen(list) + strlen(obj) + 32;
                char *body = (char *)malloc(sz);
                if (body) {
                    snprintf(body, sz, "{\"sources\":%s%s%s]}", list, empty ? "" : ",", obj);
                    char *r = local_request("POST", "/api/http/sources", body);
                    if (r) notify("PKG Manager X: added source %s", url);
                    free(r);
                    free(body);
                }
            }
        }
        free(list);
    }
    free(preset);
}

/* Starts the payload through GoldHEN's BinLoader unless the server already
 * answers. 0 when the server is up. */
static int ensure_server(void) {
    if (port_open(PKGMGR_PORT)) return 0;
    notify("Starting PKG Manager X...");
    int rc = send_payload();
    if (rc == -1) {
        notify("PKG Manager X: GoldHEN BinLoader (port 9090) is not running.\n"
               "Enable it in GoldHEN settings, or load the payload manually.");
        return -1;
    }
    if (rc != 0) {
        notify("PKG Manager X: could not send the payload (%d)", rc);
        return -1;
    }
    for (int i = 0; i < START_WAIT_SEC * 4; i++) {
        sceKernelUsleep(250 * 1000);
        if (port_open(PKGMGR_PORT)) return 0;
    }
    notify("PKG Manager X did not start. Check http://<PS4-IP>:8844/api/log");
    return -1;
}

static int user_id(void) {
    int id = -1;
    if (sceUserServiceGetForegroundUser(&id) == 0 && id != -1 && id != 0xff) return id;
    if (sceUserServiceGetInitialUser(&id) == 0) return id;
    return -1;
}

/* The dialog's magic encodes this block's address, so it must not move. */
static web_dialog_param_t g_web __attribute__((aligned(16)));

static int open_web_dialog(const char *url, int mode) {
    memset(&g_web, 0, sizeof(g_web));
    g_web.base.size = sizeof(g_web.base);
    g_web.base.magic = (uint32_t)(ORBIS_COMMON_DIALOG_MAGIC_NUMBER + (uint64_t)(uintptr_t)&g_web.base);
    g_web.size = sizeof(g_web);
    g_web.mode = mode;
    g_web.user_id = user_id();
    g_web.url = url;
    if (mode == 2) {
        g_web.width = 1920;
        g_web.height = 1080;
        g_web.header_width = 1920;
    }
    return sceWebBrowserDialogOpen(&g_web);
}

/* Full screen dialog, then the dialog's own layout. Returns 0 once the user
 * closed it with Circle, nonzero if no dialog could be opened. */
static int show_web_dialog(const char *url) {
    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_WEB_BROWSER_DIALOG) < 0) return -1;
    sceCommonDialogInitialize();
    if (sceWebBrowserDialogInitialize() < 0) return -1;
    int rc = open_web_dialog(url, 2);
    if (rc != 0) rc = open_web_dialog(url, 1);
    if (rc != 0) {
        sceWebBrowserDialogTerminate();
        return rc;
    }
    int status;
    while ((status = sceWebBrowserDialogUpdateStatus()) == ORBIS_COMMON_DIALOG_STATUS_RUNNING ||
           status == ORBIS_COMMON_DIALOG_STATUS_INITIALIZED) {
        sceKernelUsleep(16 * 1000);
    }
    sceWebBrowserDialogClose();
    sceWebBrowserDialogTerminate();
    return 0;
}

/* Leaves the app the way the system expects, back to the home screen. The
 * PKG Manager X service keeps running. */
static void exit_app(int wait_for_browser) {
    /* Browser app fallback / errors: let it and the notifications come up
     * before this app goes away. */
    if (wait_for_browser) sceKernelUsleep(2 * 1000 * 1000);
    sceUserServiceTerminate();
    sceSystemServiceLoadExec("exit", NULL);
    /* Not reached; never return from main (CE-34878-0). */
    for (;;) sceKernelUsleep(60 * 1000 * 1000);
}

int main(void) {
    sceSystemServiceHideSplashScreen();
    sceUserServiceInitialize(NULL);

    int in_dialog = 0;
    if (ensure_server() == 0) {
        apply_preset_source();
        in_dialog = show_web_dialog(UI_URL) == 0;
        if (!in_dialog) {
            int rc = sceSystemServiceLaunchWebBrowser(UI_URL, NULL);
            if (rc != 0) notify("PKG Manager X: could not open the browser (0x%08X)\nOpen %s", rc, UI_URL);
        }
    }
    exit_app(!in_dialog);
    return 0;
}
