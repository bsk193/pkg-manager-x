#ifndef PLATFORM_INSTALL_H
#define PLATFORM_INSTALL_H

/*
 * PKG Manager X - console install backends.
 *
 * Both consoles install from the same local range-streaming URL
 * (http://127.0.0.1:18841/stream/install/package-*.pkg):
 *   PS5: upstream's install service - sceAppInstUtilInstallByPackage in a
 *        fresh helper process per install (platform_install_ps5.c)
 *   PS4: BGFT background download task, in-process (platform_install_ps4.c)
 * Host test builds use neither; installer.c keeps its mock worker.
 *
 * Per install: platform_install_start -> platform_install_poll... ->
 * platform_install_close (also before every retry and on every exit path).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *uri;           /* stream URL served by stream_server.c */
    const char *display_name;  /* "<TITLE_ID> (Base)" etc. (static storage) */
    const char *title_name;    /* package title, may be "" */
    const char *title_id;
    const char *content_id;    /* parsed content ID, may be "" */
    const char *pkg_kind;      /* "base" / "update" / "dlc" */
    const char *category;      /* param category ("gd", "gp", "ac", ...) */
    uint64_t package_size;
} platform_install_request_t;

typedef struct {
    char status[16];           /* "playable", "completed", "error", "none", "downloading" */
    int32_t error_code;
    uint64_t downloaded_size;
} platform_install_progress_t;

/* platform_install_poll results besides 0 (= out is valid). */
#define PLATFORM_INSTALL_NO_STATUS (-1) /* nothing to report; rely on own checks */
#define PLATFORM_INSTALL_LOST      (-2) /* install process died / stopped answering;
                                           out->error_code holds the reason */
#define PLATFORM_INSTALL_CANCELED  (-3) /* canceled while waiting */

/* Returns non-zero when the user canceled or the daemon is shutting down. */
typedef int (*platform_install_canceled_fn)(void);

int platform_install_init(void);
void platform_install_shutdown(void);

/* Starts the system install. On success returns 0 and writes the content ID
 * the system uses to track it (may equal req->content_id). Otherwise returns
 * the (negative) system error code. */
int platform_install_start(const platform_install_request_t *req,
                           char *out_content_id, size_t content_id_size,
                           platform_install_canceled_fn canceled);

/* Queries system progress for the running install (content_id may be ""). */
int platform_install_poll(const char *content_id, platform_install_progress_t *out);

/* Releases per-install resources (PS5 helper process). Idempotent. */
void platform_install_close(void);

/* Human-readable name for an install error code, or NULL. */
const char *platform_install_strerror(int code);

/* 1 for errors worth retrying with a fresh stream session. */
int platform_install_is_transient(int code);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_INSTALL_H */
