#ifndef HTTP_TEST_SERVER_H
#define HTTP_TEST_SERVER_H

/*
 * Tiny threaded HTTP/1.1 file server for host tests of http_source.c.
 *
 *  - GET/HEAD with single "Range: bytes=a-b" (206) and keep-alive
 *  - directories -> Apache-style HTML listing (parent, sort and icon links
 *    included so the client's link filtering is exercised)
 *  - /redir/<path> -> 302 to /<path>
 *  - optional Basic auth, optional "ignore Range" mode
 *  - optional nginx "autoindex_format json" listings
 *  - optional home-server gateway emulation: /files/<path> -> 302 to
 *    http://localhost:<port>/signed/<expiry>/<path> (another host name, no
 *    auth needed, 403 once expired, like a signed Cloudflare R2 URL).
 *    /api/catalog is simply a file under root.
 */

typedef struct {
    const char *root;   /* directory to serve */
    const char *user;   /* NULL = no auth */
    const char *pass;
    int no_range;       /* 1 = always answer 200 with the full body */
    int port;           /* out: bound port on 127.0.0.1 */
    int json_listing;   /* 1 = directories as nginx JSON */
    int gateway;        /* 1 = /files/ + /signed/ emulation */
    int sign_ttl;       /* signed link lifetime in seconds (default 3600) */
} http_test_server_opts_t;

typedef struct {
    int files_hits;     /* /files/ requests (initial links + refreshes) */
    int signed_hits;    /* /signed/ requests */
    int signed_expired; /* 403s for expired signed links */
    int leaked_auth;    /* Authorization headers seen on /signed/ */
} http_test_server_stats_t;

int http_test_server_start(http_test_server_opts_t *opts);
void http_test_server_stop(void);
void http_test_server_get_stats(http_test_server_stats_t *out);
void http_test_server_set_sign_ttl(int seconds);

#endif /* HTTP_TEST_SERVER_H */
