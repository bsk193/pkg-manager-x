#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stddef.h>
#include "platform.h"

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sends a PS5 on-screen system notification.
 */
void ps5_notify(const char *fmt, ...);

#if !PKGMGR_ON_CONSOLE
/* Host tests only: notifications sent since the last reset, oldest first
 * (up to 64 are kept). Console builds have none of this. */
void notification_test_reset(void);
int notification_test_count(void);
const char *notification_test_get(int index);
#endif

#ifdef __cplusplus
}
#endif

#endif /* NOTIFICATION_H */
