/*
 * PKG Manager X - Transport-Agnostic PKG Metadata Parser
 *
 * Mirrors the classification rules of pkg_parser.c / smb_client.c over a
 * read callback so remote sources do not need another copy of the parser.
 */

#include "pkg_parse_reader.h"
#include "pkg_platform.h"
#include "multipart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint64_t rd_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (i * 8);
    return v;
}

typedef struct {
    pkg_read_fn rd;
    void *ctx;
    uint64_t file_size;
    uint8_t *prefix;
    size_t prefix_len;
} reader_t;

/* Exact read: serves from the prefix window when fully covered, otherwise
 * one callback read. Returns 0 only when all count bytes were read. */
static int reader_get(reader_t *r, void *buf, size_t count, uint64_t offset) {
    if (count == 0) return 0;
    if (r->file_size > 0 && (offset > r->file_size || count > r->file_size - offset)) {
        return -1;
    }
    if (r->prefix && offset < r->prefix_len && count <= r->prefix_len - offset) {
        memcpy(buf, r->prefix + offset, count);
        return 0;
    }
    size_t done = 0;
    while (done < count) {
        ssize_t n = r->rd(r->ctx, (uint8_t *)buf + done, count - done, offset + done);
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static void set_type_str(pkg_detail_t *out) {
    switch (out->pkg_type) {
        case PKG_TYPE_BASE:   strncpy(out->pkg_type_str, "base", sizeof(out->pkg_type_str) - 1); break;
        case PKG_TYPE_UPDATE: strncpy(out->pkg_type_str, "update", sizeof(out->pkg_type_str) - 1); break;
        case PKG_TYPE_DLC:    strncpy(out->pkg_type_str, "dlc", sizeof(out->pkg_type_str) - 1); break;
        default:              strncpy(out->pkg_type_str, "unknown", sizeof(out->pkg_type_str) - 1); break;
    }
}

int pkg_parse_reader(pkg_read_fn rd, void *ctx, uint64_t file_size,
                     const char *path, pkg_detail_t *out) {
    if (!rd || !out) return -1;

    memset(out, 0, sizeof(*out));
    if (path) {
        strncpy(out->path, path, sizeof(out->path) - 1);
        const char *slash = strrchr(path, '/');
        strncpy(out->filename, (slash && slash[1]) ? slash + 1 : path, sizeof(out->filename) - 1);
    }
    out->file_size = file_size;
    out->total_pkg_size = file_size;

    reader_t r;
    memset(&r, 0, sizeof(r));
    r.rd = rd;
    r.ctx = ctx;
    r.file_size = file_size;

    size_t want = PKG_PARSE_READER_PREFIX;
    if (file_size > 0 && file_size < want) want = (size_t)file_size;
    r.prefix = (uint8_t *)malloc(want);
    if (!r.prefix) return -1;
    while (r.prefix_len < want) {
        ssize_t n = rd(ctx, r.prefix + r.prefix_len, want - r.prefix_len, r.prefix_len);
        if (n <= 0) break;
        r.prefix_len += (size_t)n;
    }
    if (r.prefix_len < 0x80) {
        free(r.prefix);
        return -1;
    }

    const uint8_t *hdr = r.prefix;
    size_t hdr_read = r.prefix_len < 0x200 ? r.prefix_len : 0x200;

    if (hdr_read >= MULTIPART_MAGIC_LEN && memcmp(hdr, MULTIPART_MAGIC, MULTIPART_MAGIC_LEN) == 0) {
        free(r.prefix);
        return -2; /* multi-part is supported on local drives only */
    }

    pkg_platform_note_header(out, hdr, hdr_read);

    uint64_t cnt_offset = 0;
    int cnt_found = 0;
    if (memcmp(hdr, "\x7f" "CNT", 4) == 0) {
        cnt_found = 1;
    } else if (memcmp(hdr, "\x7f" "FIH", 4) == 0) {
        uint8_t magic[4];
        uint64_t cand = rd_le64(hdr + 0x58);
        if (cand > 0 && (file_size == 0 || cand < file_size) &&
            reader_get(&r, magic, 4, cand) == 0 && memcmp(magic, "\x7f" "CNT", 4) == 0) {
            cnt_offset = cand;
            cnt_found = 1;
        }
        for (size_t off = 0x10; !cnt_found && off + 8 <= hdr_read; off += 0x08) {
            cand = rd_le64(hdr + off);
            if (cand >= 0x10000 && (file_size == 0 || cand < file_size) && (cand % 0x1000) == 0 &&
                reader_get(&r, magic, 4, cand) == 0 && memcmp(magic, "\x7f" "CNT", 4) == 0) {
                cnt_offset = cand;
                cnt_found = 1;
            }
        }
    }
    if (!cnt_found) {
        free(r.prefix);
        return -1;
    }

    uint8_t cnt_hdr[0x80];
    if (reader_get(&r, cnt_hdr, sizeof(cnt_hdr), cnt_offset) != 0 ||
        memcmp(cnt_hdr, "\x7f" "CNT", 4) != 0) {
        free(r.prefix);
        return -1;
    }

    uint32_t cnt_type_magic = rd_be32(cnt_hdr + 0x04);
    memcpy(out->content_id, cnt_hdr + 0x40, 48);
    out->content_id[48] = '\0';
    for (int i = 0; i < 48; i++) {
        if ((unsigned char)out->content_id[i] < 32 || (unsigned char)out->content_id[i] > 126) {
            out->content_id[i] = '\0';
            break;
        }
    }

    uint32_t entry_count = rd_be32(cnt_hdr + 0x10);
    uint32_t table_offset = rd_be32(cnt_hdr + 0x18);
    if (entry_count == 0 || entry_count > 2048 || table_offset > 0x200000) {
        free(r.prefix);
        return -1;
    }

    size_t table_size = (size_t)entry_count * 32;
    uint8_t *entry_table = (uint8_t *)malloc(table_size);
    if (!entry_table || reader_get(&r, entry_table, table_size, cnt_offset + table_offset) != 0) {
        free(entry_table);
        free(r.prefix);
        return -1;
    }

    uint32_t str_table_off = 0, str_table_sz = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        const uint8_t *e = entry_table + i * 32;
        if (rd_be32(e) == 0x0200) {
            str_table_off = rd_be32(e + 16);
            str_table_sz = rd_be32(e + 20);
            break;
        }
    }

    char *str_table = NULL;
    if (str_table_sz > 0 && str_table_sz < 65536) {
        str_table = (char *)malloc(str_table_sz + 1);
        if (str_table) {
            if (reader_get(&r, str_table, str_table_sz, cnt_offset + str_table_off) == 0) {
                str_table[str_table_sz] = '\0';
            } else {
                free(str_table);
                str_table = NULL;
            }
        }
    }

    int has_playgo_chunk_patch = 0;
    int has_delta_patch = 0;
    int has_base_app_metadata = 0;
    int has_ps4_sfo_category = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        const uint8_t *e = entry_table + i * 32;
        uint32_t type = rd_be32(e);
        uint32_t fn_off = rd_be32(e + 4);
        uint32_t data_off = rd_be32(e + 16);
        uint32_t data_sz = rd_be32(e + 20);

        const char *name = "";
        if (str_table && fn_off < str_table_sz) name = str_table + fn_off;

        if (type == 0x1008 || strcmp(name, "app/playgo-chunk.dat") == 0) {
            has_playgo_chunk_patch = 1;
        }
        if (type == 0x0407 || type == 0x0408 ||
            strcmp(name, "target-deltainfo.dat") == 0 || strcmp(name, "origin-deltainfo.dat") == 0) {
            has_delta_patch = 1;
        }

        if ((type == 0x2000 || strcmp(name, "param.json") == 0) && data_sz > 0 && data_sz < 262144) {
            char *json_buf = (char *)malloc(data_sz + 1);
            if (json_buf) {
                if (reader_get(&r, json_buf, data_sz, cnt_offset + data_off) == 0) {
                    json_buf[data_sz] = '\0';
                    pkg_platform_note_param_json(out);
                    if (strstr(json_buf, "\"applicationDrmType\"") ||
                        strstr(json_buf, "\"applicationCategoryType\"") ||
                        strstr(json_buf, "\"contentBadgeType\"")) {
                        has_base_app_metadata = 1;
                    }
                    pkg_parser_parse_param_json(json_buf, data_sz,
                                                out->title_id, sizeof(out->title_id),
                                                out->title_name, sizeof(out->title_name),
                                                out->category, sizeof(out->category),
                                                out->app_version, sizeof(out->app_version),
                                                out->localized_titles, sizeof(out->localized_titles),
                                                out->default_language, sizeof(out->default_language));
                }
                free(json_buf);
            }
        }

        if ((type == 0x1000 || strcmp(name, "param.sfo") == 0) && data_sz > 0 && data_sz < 262144) {
            uint8_t *sfo_buf = (uint8_t *)malloc(data_sz);
            if (sfo_buf) {
                if (reader_get(&r, sfo_buf, data_sz, cnt_offset + data_off) == 0) {
                    pkg_platform_note_param_sfo(out);
                    char stitle[PKG_TITLE_NAME_LEN] = {0};
                    char stid[PKG_TITLE_ID_LEN] = {0};
                    char sver[32] = {0};
                    char sfo_category[sizeof(out->category)] = {0};
                    pkg_parser_parse_param_sfo(sfo_buf, data_sz, stitle, sizeof(stitle),
                                               stid, sizeof(stid), sver, sizeof(sver),
                                               sfo_category, sizeof(sfo_category),
                                               out->localized_titles, sizeof(out->localized_titles),
                                               out->default_language, sizeof(out->default_language));
                    if (sfo_category[0] != '\0' && out->category[0] == '\0') {
                        has_ps4_sfo_category = 1;
                        strncpy(out->category, sfo_category, sizeof(out->category) - 1);
                    }
                    if (out->title_id[0] == '\0' && stid[0] != '\0') {
                        strncpy(out->title_id, stid, sizeof(out->title_id) - 1);
                    }
                    if (out->title_name[0] == '\0' && stitle[0] != '\0') {
                        strncpy(out->title_name, stitle, sizeof(out->title_name) - 1);
                    }
                    if (out->app_version[0] == '\0' && sver[0] != '\0') {
                        strncpy(out->app_version, sver, sizeof(out->app_version) - 1);
                    }
                }
                free(sfo_buf);
            }
        }

        if ((type == 0x1200 || strcmp(name, "icon0.png") == 0) &&
            data_sz > 0 && data_sz < 10 * 1024 * 1024 &&
            (file_size == 0 || cnt_offset + (uint64_t)data_off + (uint64_t)data_sz <= file_size)) {
            out->has_icon = 1;
            out->icon_offset = cnt_offset + data_off;
            out->icon_size = data_sz;
        }
    }

    int is_delta_type = ((cnt_type_magic & 0xFF) == 0x1E || (cnt_type_magic & 0xFF000000) == 0x41000000);
    if (has_playgo_chunk_patch || has_delta_patch || is_delta_type ||
        (out->category[0] != '\0' && strncmp(out->category, "gp", 2) == 0)) {
        out->pkg_type = PKG_TYPE_UPDATE;
    } else if (strncmp(out->category, "ac", 2) == 0 || strncmp(out->category, "al", 2) == 0 ||
               strcmp(out->category, "addcont") == 0) {
        out->pkg_type = PKG_TYPE_DLC;
    } else if (has_ps4_sfo_category &&
               (strncmp(out->category, "gd", 2) == 0 || strncmp(out->category, "bd", 2) == 0 ||
                strncmp(out->category, "gc", 2) == 0 || strncmp(out->category, "wt", 2) == 0)) {
        out->pkg_type = PKG_TYPE_BASE;
    } else if ((cnt_type_magic & 0xFF) == 1 && !has_base_app_metadata) {
        out->pkg_type = PKG_TYPE_DLC;
    } else if (strncmp(out->category, "gd", 2) == 0 || strncmp(out->category, "bd", 2) == 0 ||
               strncmp(out->category, "gc", 2) == 0 || strncmp(out->category, "wt", 2) == 0) {
        out->pkg_type = PKG_TYPE_BASE;
    }
    if (out->pkg_type == PKG_TYPE_UNKNOWN) out->pkg_type = PKG_TYPE_BASE;
    set_type_str(out);

    free(str_table);
    free(entry_table);
    free(r.prefix);

    if (out->title_id[0] == '\0' && out->content_id[0] != '\0') {
        const char *dash = strchr(out->content_id, '-');
        if (dash) {
            const char *us = strchr(dash + 1, '_');
            if (us && (size_t)(us - (dash + 1)) < sizeof(out->title_id)) {
                size_t len = (size_t)(us - (dash + 1));
                memcpy(out->title_id, dash + 1, len);
                out->title_id[len] = '\0';
            }
        }
    }
    if (out->title_name[0] == '\0') {
        snprintf(out->title_name, sizeof(out->title_name), "%s",
                 out->title_id[0] ? out->title_id : "Unknown Package");
    }

    out->is_valid = 1;
    return 0;
}
