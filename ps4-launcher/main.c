/*
 * PKG Manager X - PS4 home screen tile.
 *
 * PS4 has no "web link" tile like the PS5 deeplink shortcut, so this tiny
 * app does the same job:
 *   1. If PKG Manager X is not running (nothing on 127.0.0.1:8844), send the
 *      bundled payload (/app0/pkgmgr-ps4.elf) to GoldHEN's BinLoader on
 *      127.0.0.1:9090 and wait for the web server to come up.
 *   2. Open the console browser at http://127.0.0.1:8844/ and exit.
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

#define PKGMGR_PORT     8844
#define BINLOADER_PORT  9090
#define PAYLOAD_PATH    "/app0/pkgmgr-ps4.elf"
#define UI_URL          "http://127.0.0.1:8844/"
#define START_WAIT_SEC  25

int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);
int sceSystemServiceLaunchWebBrowser(const char *uri, void *param);
int sceSystemServiceHideSplashScreen(void);
int sceKernelUsleep(unsigned int usec);

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

int main(void) {
    sceSystemServiceHideSplashScreen();

    if (!port_open(PKGMGR_PORT)) {
        notify("Starting PKG Manager X...");
        int rc = send_payload();
        if (rc == -1) {
            notify("PKG Manager X: GoldHEN BinLoader (port 9090) is not running.\n"
                   "Enable it in GoldHEN settings, or load the payload manually.");
            return 0;
        }
        if (rc != 0) {
            notify("PKG Manager X: could not send the payload (%d)", rc);
            return 0;
        }
        int up = 0;
        for (int i = 0; i < START_WAIT_SEC * 4 && !up; i++) {
            sceKernelUsleep(250 * 1000);
            up = port_open(PKGMGR_PORT);
        }
        if (!up) {
            notify("PKG Manager X did not start. Check http://<PS4-IP>:8844/api/log");
            return 0;
        }
    }

    sceUserServiceInitialize(NULL);
    int rc = sceSystemServiceLaunchWebBrowser(UI_URL, NULL);
    if (rc != 0) notify("PKG Manager X: could not open the browser (0x%08X)\nOpen %s", rc, UI_URL);
    /* Give the system time to bring the browser up before this app exits. */
    sceKernelUsleep(2 * 1000 * 1000);
    sceUserServiceTerminate();
    return 0;
}
