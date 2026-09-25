#ifndef PKG_PARSE_READER_H
#define PKG_PARSE_READER_H

/*
 * PKG Manager X - transport-agnostic PKG metadata parser.
 *
 * Same classification rules as pkg_parser_parse() / smb_client_parse_pkg(),
 * but every byte comes through a random-access read callback, so any
 * remote source (HTTP/HTTPS today) can reuse it. A prefix window is fetched
 * once and serves the header, entry table and param files of typical
 * packages without extra round trips.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "pkg_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns bytes read (short only at EOF) or negative on error. */
typedef ssize_t (*pkg_read_fn)(void *ctx, void *buf, size_t count, uint64_t offset);

/* Prefix window fetched up front (bytes). */
#define PKG_PARSE_READER_PREFIX (256 * 1024)

/* Parses PKG metadata. path is stored in out->path (filename derived from
 * it). Multi-part containers are rejected (-2), matching SMB behavior.
 * Returns 0 on success, negative on failure. */
int pkg_parse_reader(pkg_read_fn rd, void *ctx, uint64_t file_size,
                     const char *path, pkg_detail_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PKG_PARSE_READER_H */
