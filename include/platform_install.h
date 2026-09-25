#ifndef PLATFORM_INSTALL_H
#define PLATFORM_INSTALL_H

/*
 * PKG Manager X - console install backends.
 *
 * Both consoles install from the same local range-streaming URL
 * (http://127.0.0.1:18841/stream/install/package-*.pkg):
 *   PS5: sceAppInstUtilInstallByPackage   (platform_install_ps5.c)
 *   PS4: BGFT background download task    (platform_install_ps4.c)
 * Host test builds use neither; installer.c keeps its mock worker.
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

int platform_install_init(void);
void platform_install_shutdown(void);

/* Starts the system install. On success returns 0 and writes the content ID
 * the system uses to track it (may equal req->content_id). Otherwise returns
 * the (negative) system error code. */
int platform_install_start(const platform_install_request_t *req,
                           char *out_content_id, size_t content_id_size);

/* Queries system progress for content_id. 0 when out is valid, negative
 * when the console offers no status (caller relies on its own checks). */
int platform_install_poll(const char *content_id, platform_install_progress_t *out);

/* Human-readable name for an install error code, or NULL. */
const char *platform_install_strerror(int code);

/* 1 for errors worth retrying with a fresh stream session. */
int platform_install_is_transient(int code);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_INSTALL_H */
