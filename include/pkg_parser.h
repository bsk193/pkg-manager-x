#ifndef PKG_PARSER_H
#define PKG_PARSER_H

#include <stdint.h>
#include <stddef.h>

#define PKG_TITLE_ID_LEN 32
#define PKG_TITLE_NAME_LEN 256
#define PKG_CONTENT_ID_LEN 64
#define PKG_PATH_LEN 512

typedef enum {
    PKG_TYPE_UNKNOWN = 0,
    PKG_TYPE_BASE,     /* Base application / package */
    PKG_TYPE_UPDATE,   /* Application update / patch */
    PKG_TYPE_DLC       /* Additional content / DLC */
} pkg_type_t;

typedef struct {
    char path[PKG_PATH_LEN];
    char filename[256];
    char title_id[PKG_TITLE_ID_LEN];
    char title_name[PKG_TITLE_NAME_LEN];
    char content_id[PKG_CONTENT_ID_LEN];
    char app_version[32];
    uint64_t file_size;
    uint64_t total_pkg_size;
    uint64_t icon_offset;
    uint32_t icon_size;
    int has_icon;
    int is_valid;
    int is_multipart;
    uint32_t part_index;
    uint32_t total_parts;
    pkg_type_t pkg_type;
    char pkg_type_str[16];   /* "base", "update", "dlc", "unknown" */
    char category[16];       /* e.g. "gd", "gp", "ac", etc. */
    uint64_t mtime;          /* File modification timestamp */
    char blurhash[64];       /* BlurHash placeholder for icon0.png ("" if none) */
} pkg_detail_t;

/**
 * Parses a PKG file (PS5 FIH or PS4 CNT format) and extracts metadata:
 * title_id, title_name, content_id, and icon0.png offset/size.
 * Returns 0 on success, negative value on error.
 */
int pkg_parser_parse(const char *file_path, pkg_detail_t *out);

/**
 * Extracts raw icon bytes from the PKG file.
 * Allocates *out_data with malloc, which caller must free().
 * Returns 0 on success, negative on error.
 */
int pkg_parser_get_icon(const char *file_path, uint64_t offset, uint32_t size,
                        uint8_t **out_data, size_t *out_size);

#endif /* PKG_PARSER_H */
