#ifndef HTTP_SOURCES_API_H
#define HTTP_SOURCES_API_H

/*
 * PKG Manager X - REST routes added by the fork (kept out of http_server.c
 * so upstream merges stay small):
 *
 *   GET  /api/platform       console, installable platforms, capabilities,
 *                            version + upstream_version ("based on")
 *   GET  /api/http/sources   configured HTTP sources (passwords masked)
 *   POST /api/http/sources   replace list: {"sources":[...]}; a blank password
 *                            keeps the stored one for the same id
 *   POST /api/http/test      test one source config (not saved)
 */

#include <microhttpd.h>

/* Returns 1 and sets *out_ret when url is one of the routes above. */
int http_sources_api_handle(struct MHD_Connection *conn, const char *url, const char *method,
                            const char *body, enum MHD_Result *out_ret);

#endif /* HTTP_SOURCES_API_H */
