/*
 * PKG Manager - System Notification Dispatcher
 */

#include "notification.h"
#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Same request layout on PS4 and PS5 (message at offset 45, 3120 bytes). */
#if PKGMGR_ON_CONSOLE
int sceKernelSendNotificationRequest(int device, notify_request_t *request,
                                     size_t size, int unused);
#endif

#if !PKGMGR_ON_CONSOLE
#include <pthread.h>

#define NOTIFY_TEST_MAX 64
static pthread_mutex_t g_notify_test_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_notify_test_msgs[NOTIFY_TEST_MAX][256];
static int g_notify_test_count;

static void notification_test_record(const char *msg) {
    pthread_mutex_lock(&g_notify_test_lock);
    if (g_notify_test_count < NOTIFY_TEST_MAX) {
        snprintf(g_notify_test_msgs[g_notify_test_count++], sizeof(g_notify_test_msgs[0]), "%s", msg);
    }
    pthread_mutex_unlock(&g_notify_test_lock);
}

void notification_test_reset(void) {
    pthread_mutex_lock(&g_notify_test_lock);
    g_notify_test_count = 0;
    pthread_mutex_unlock(&g_notify_test_lock);
}

int notification_test_count(void) {
    pthread_mutex_lock(&g_notify_test_lock);
    int n = g_notify_test_count;
    pthread_mutex_unlock(&g_notify_test_lock);
    return n;
}

const char *notification_test_get(int index) {
    return (index >= 0 && index < notification_test_count()) ? g_notify_test_msgs[index] : NULL;
}
#endif

void ps5_notify(const char *fmt, ...) {
    notify_request_t req;
    va_list args;

    memset(&req, 0, sizeof(req));
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);

#if PKGMGR_ON_CONSOLE
    int result = sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    if (result != 0) {
        fprintf(stderr, "[PKG Manager] Notification failed (0x%08X): %s\n",
                result, req.message);
    }
#else
    printf("[PS5 Notification] %s\n", req.message);
    notification_test_record(req.message);
#endif
}
