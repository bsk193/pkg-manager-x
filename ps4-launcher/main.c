/*
 * PKG Manager X - PS4 home screen tile.
 *
 * PS4 has no "web link" tile like the PS5 deeplink shortcut, so this tiny
 * app does the same job:
 *   1. If PKG Manager X is not running (nothing on 127.0.0.1:8844), send the
 *      bundled payload (/app0/pkgmgr-ps4.elf) to GoldHEN's BinLoader on
 *      127.0.0.1:9090 and wait for the web server to come up.
 *   2. Open the console browser at http://127.0.0.1:8844/.
 * The app then stays in the background. Whenever it gets the focus back
 * (Circle in the browser, or the tile opened again from the home screen) it
 * reopens the browser instead of showing a black screen; any controller
 * button on the black screen does the same. PS4 apps must not return from
 * main: the system reports that as a crash (CE-34878-0) and GoldHEN may take
 * the payload it started for us down with it. Closing the app from the home
 * screen ends it cleanly.
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
#include <time.h>
#include <orbis/Pad.h>

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
int sceUserServiceGetInitialUser(int *user_id);

/* Set in OrbisPadData.buttons while another app / the system UI has focus. */
#define PAD_BUTTON_INTERCEPTED 0x80000000u
#define POLL_USEC          (250 * 1000)
#define RESUME_GAP_SEC     2   /* loop stalled this long => we were suspended */
#define RELAUNCH_COOLDOWN  3   /* seconds between browser launches */

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

static time_t g_last_launch;

static void open_browser(void) {
    g_last_launch = time(NULL);
    if (ensure_server() != 0) return;
    int rc = sceSystemServiceLaunchWebBrowser(UI_URL, NULL);
    if (rc != 0) notify("PKG Manager X: could not open the browser (0x%08X)\nOpen %s", rc, UI_URL);
    g_last_launch = time(NULL);
}

int main(void) {
    sceSystemServiceHideSplashScreen();
    sceUserServiceInitialize(NULL);

    int pad = -1;
    int user = -1;
    if (scePadInit() == 0 && sceUserServiceGetInitialUser(&user) == 0) {
        pad = scePadOpen(user, 0, 0, NULL);
    }

    open_browser();

    /* Never returns; the user closes the app from the home screen. */
    uint32_t prev_buttons = 0;
    int had_focus_loss = 0;
    time_t prev_tick = time(NULL);
    for (;;) {
        sceKernelUsleep(POLL_USEC);
        time_t now = time(NULL);
        int reopen = 0;

        /* Suspended while the browser was in front, now running again. */
        if (now - prev_tick >= RESUME_GAP_SEC) reopen = 1;
        prev_tick = now;

        if (pad >= 0) {
            OrbisPadData d;
            memset(&d, 0, sizeof(d));
            if (scePadReadState(pad, &d) == 0 && d.connected) {
                if (d.buttons & PAD_BUTTON_INTERCEPTED) {
                    had_focus_loss = 1;            /* browser / system UI in front */
                } else {
                    if (had_focus_loss) reopen = 1; /* focus is back on the black screen */
                    had_focus_loss = 0;
                    uint32_t pressed = d.buttons & ~prev_buttons;
                    if (pressed) reopen = 1;        /* any button on the black screen */
                }
                prev_buttons = d.buttons & ~PAD_BUTTON_INTERCEPTED;
            }
        }

        if (reopen && now - g_last_launch >= RELAUNCH_COOLDOWN) {
            open_browser();
            prev_tick = time(NULL);
        }
    }
    return 0;
}
