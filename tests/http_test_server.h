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
 */

typedef struct {
    const char *root;   /* directory to serve */
    const char *user;   /* NULL = no auth */
    const char *pass;
    int no_range;       /* 1 = always answer 200 with the full body */
    int port;           /* out: bound port on 127.0.0.1 */
} http_test_server_opts_t;

int http_test_server_start(http_test_server_opts_t *opts);
void http_test_server_stop(void);

#endif /* HTTP_TEST_SERVER_H */
