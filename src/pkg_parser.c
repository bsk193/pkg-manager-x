/*
 * PKG Manager - Binary Package & Metadata Parser
 *
 * Reads PS4 (CNT) and PS5 (FIH) package headers, parses param.sfo /
 * param.json tables, and extracts embedded icon0.png artwork.
 */

#include "pkg_parser.h"
#include "multipart.h"
#include "smb_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

/* Endian helpers */
static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t read_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)p[i]) << (i * 8);
    }
    return v;
}

/* Helper to extract a string value for a given key from JSON */
static int json_extract_key(const char *json, size_t json_len, const char *key, char *out, size_t out_max) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = json;
    const char *end = json + json_len;

    while (p < end) {
        const char *found = strstr(p, pattern);
        if (!found || found >= end) {
            return -1;
        }

        const char *colon = strchr(found + strlen(pattern), ':');
        if (!colon || colon >= end) {
            return -1;
        }

        const char *q = colon + 1;
        while (q < end && isspace((unsigned char)*q)) {
            q++;
        }

        if (q < end && *q == '"') {
            q++;
            size_t idx = 0;
            while (q < end && *q != '"') {
                if (*q == '\\' && (q + 1) < end) {
                    q++;
                }
                if (idx + 1 < out_max) {
                    out[idx++] = *q;
                }
                q++;
            }
            out[idx] = '\0';
            return 0;
        }

        /* If value was not a string (e.g. nested object or null), keep searching next occurrence */
        p = found + strlen(pattern);
    }

    return -1;
}

/* Helper to parse PS4 param.sfo */
static void parse_param_sfo(const uint8_t *sfo, size_t sfo_len, char *out_title, size_t title_max,
                            char *out_title_id, size_t title_id_max,
                            char *out_version, size_t version_max,
                            char *out_category, size_t category_max) {
    if (sfo_len < 20 || memcmp(sfo, "\x00PSF", 4) != 0) {
        return;
    }

    uint32_t key_table_start = read_le32(sfo + 0x08);
    uint32_t data_table_start = read_le32(sfo + 0x0C);
    uint32_t entry_count = read_le32(sfo + 0x10);

    if (key_table_start >= sfo_len || data_table_start >= sfo_len || entry_count > 1024) {
        return;
    }

    char sfo_app_ver[32] = {0};
    char sfo_version[32] = {0};

    const uint8_t *entries = sfo + 20;
    for (uint32_t i = 0; i < entry_count; i++) {
        if ((size_t)(20 + (i + 1) * 16) > sfo_len) {
            break;
        }
        const uint8_t *e = entries + i * 16;
        uint16_t key_off = read_le16(e);
        uint32_t data_len = read_le32(e + 4);
        uint32_t data_off = read_le32(e + 12);

        if (key_table_start + key_off >= sfo_len) continue;
        const char *key = (const char *)(sfo + key_table_start + key_off);
        size_t max_key_len = sfo_len - (key_table_start + key_off);
        if (!memchr(key, '\0', max_key_len)) continue;

        if ((uint64_t)data_off > sfo_len || (uint64_t)data_len > sfo_len ||
            (uint64_t)data_table_start + (uint64_t)data_off + (uint64_t)data_len > (uint64_t)sfo_len) continue;
        const char *data = (const char *)(sfo + data_table_start + data_off);

        if (strcmp(key, "TITLE") == 0 && out_title[0] == '\0') {
            size_t copy_len = data_len < title_max ? data_len : title_max - 1;
            /* Strip trailing null if included in data_len */
            while (copy_len > 0 && data[copy_len - 1] == '\0') copy_len--;
            strncpy(out_title, data, copy_len);
            out_title[copy_len] = '\0';
        } else if (strcmp(key, "TITLE_ID") == 0 && out_title_id[0] == '\0') {
            size_t copy_len = data_len < title_id_max ? data_len : title_id_max - 1;
            while (copy_len > 0 && data[copy_len - 1] == '\0') copy_len--;
            strncpy(out_title_id, data, copy_len);
            out_title_id[copy_len] = '\0';
        } else if (strcmp(key, "CATEGORY") == 0 && out_category && out_category[0] == '\0') {
            size_t copy_len = data_len < category_max ? data_len : category_max - 1;
            while (copy_len > 0 && data[copy_len - 1] == '\0') copy_len--;
            strncpy(out_category, data, copy_len);
            out_category[copy_len] = '\0';
        } else if (strcmp(key, "APP_VER") == 0 && sfo_app_ver[0] == '\0') {
            size_t copy_len = data_len < sizeof(sfo_app_ver) ? data_len : sizeof(sfo_app_ver) - 1;
            while (copy_len > 0 && data[copy_len - 1] == '\0') copy_len--;
            strncpy(sfo_app_ver, data, copy_len);
            sfo_app_ver[copy_len] = '\0';
        } else if (strcmp(key, "VERSION") == 0 && sfo_version[0] == '\0') {
            size_t copy_len = data_len < sizeof(sfo_version) ? data_len : sizeof(sfo_version) - 1;
            while (copy_len > 0 && data[copy_len - 1] == '\0') copy_len--;
            strncpy(sfo_version, data, copy_len);
            sfo_version[copy_len] = '\0';
        }
    }

    const char *best_v = (sfo_app_ver[0] != '\0') ? sfo_app_ver : sfo_version;
    if (best_v && best_v[0] != '\0' && out_version && out_version[0] == '\0') {
        if (best_v[0] != 'v' && best_v[0] != 'V') {
            snprintf(out_version, version_max, "v%s", best_v);
        } else {
            strncpy(out_version, best_v, version_max - 1);
            out_version[version_max - 1] = '\0';
        }
    }
}

int pkg_parser_parse(const char *file_path, pkg_detail_t *out) {
    if (!file_path || !out) {
        return -1;
    }

    if (strncmp(file_path, "smb://", 6) == 0) {
        return smb_client_parse_pkg(file_path, out);
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->path, file_path, sizeof(out->path) - 1);

    /* Extract filename from path */
    const char *slash = strrchr(file_path, '/');
    if (slash) {
        strncpy(out->filename, slash + 1, sizeof(out->filename) - 1);
    } else {
        strncpy(out->filename, file_path, sizeof(out->filename) - 1);
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == 0) {
        out->file_size = (uint64_t)st.st_size;
        out->total_pkg_size = (uint64_t)st.st_size;
        out->mtime = (uint64_t)st.st_mtime;
    }

    uint8_t hdr[0x200];
    ssize_t hdr_read = pread(fd, hdr, sizeof(hdr), 0);
    if (hdr_read < 0x80) {
        close(fd);
        return -1;
    }

    /* Check for multi-part archive format (PS5MPKG1) */
    if (hdr_read >= MULTIPART_MAGIC_LEN && memcmp(hdr, MULTIPART_MAGIC, MULTIPART_MAGIC_LEN) == 0) {
        multipart_header_t mhdr;
        if (pread(fd, &mhdr, sizeof(mhdr), 0) == sizeof(mhdr) && multipart_is_valid_header(&mhdr)) {
            close(fd);
            /* Disk-controlled strings may lack NUL termination: force it
             * before any strcmp/strncpy to avoid OOB stack reads. */
            mhdr.pkg_type[sizeof(mhdr.pkg_type) - 1] = '\0';
            mhdr.title_id[sizeof(mhdr.title_id) - 1] = '\0';
            mhdr.title_name[sizeof(mhdr.title_name) - 1] = '\0';
            mhdr.content_id[sizeof(mhdr.content_id) - 1] = '\0';
            mhdr.app_version[sizeof(mhdr.app_version) - 1] = '\0';
            mhdr.pkg_filename[sizeof(mhdr.pkg_filename) - 1] = '\0';
            out->is_multipart = 1;
            out->part_index = mhdr.part_index;
            out->total_parts = mhdr.total_parts;
            if (mhdr.pkg_type[0] != '\0') {
                if (strcmp(mhdr.pkg_type, "update") == 0) {
                    out->pkg_type = PKG_TYPE_UPDATE;
                    strncpy(out->pkg_type_str, "update", sizeof(out->pkg_type_str) - 1);
                } else if (strcmp(mhdr.pkg_type, "dlc") == 0) {
                    out->pkg_type = PKG_TYPE_DLC;
                    strncpy(out->pkg_type_str, "dlc", sizeof(out->pkg_type_str) - 1);
                } else {
                    out->pkg_type = PKG_TYPE_BASE;
                    strncpy(out->pkg_type_str, "base", sizeof(out->pkg_type_str) - 1);
                }
            } else {
                out->pkg_type = PKG_TYPE_BASE;
                strncpy(out->pkg_type_str, "base", sizeof(out->pkg_type_str) - 1);
            }
            if (mhdr.title_id[0] != '\0') {
                strncpy(out->title_id, mhdr.title_id, sizeof(out->title_id) - 1);
                out->title_id[sizeof(out->title_id) - 1] = '\0';
            } else {
                strncpy(out->title_id, "UNKNOWN", sizeof(out->title_id) - 1);
                out->title_id[sizeof(out->title_id) - 1] = '\0';
            }
            if (mhdr.title_name[0] != '\0') {
                strncpy(out->title_name, mhdr.title_name, sizeof(out->title_name) - 1);
                out->title_name[sizeof(out->title_name) - 1] = '\0';
            } else if (out->title_id[0] != '\0') {
                strncpy(out->title_name, out->title_id, sizeof(out->title_name) - 1);
                out->title_name[sizeof(out->title_name) - 1] = '\0';
            } else {
                strncpy(out->title_name, "Unknown Package", sizeof(out->title_name) - 1);
                out->title_name[sizeof(out->title_name) - 1] = '\0';
            }
            if (mhdr.app_version[0] != '\0') {
                strncpy(out->app_version, mhdr.app_version, sizeof(out->app_version) - 1);
                out->app_version[sizeof(out->app_version) - 1] = '\0';
            }
            if (mhdr.content_id[0] != '\0') {
                strncpy(out->content_id, mhdr.content_id, sizeof(out->content_id) - 1);
                out->content_id[sizeof(out->content_id) - 1] = '\0';
            }
            out->file_size = (uint64_t)st.st_size;
            out->total_pkg_size = mhdr.total_pkg_size > 0 ? mhdr.total_pkg_size : out->file_size;
            /* Cap icon size: crafted headers with multi-GB sizes would OOM
             * the scanner (payload heap is only a few hundred MB). */
            if (mhdr.icon_offset > 0 && mhdr.icon_size > 0 &&
                mhdr.icon_size < 10 * 1024 * 1024) {
                out->icon_offset = mhdr.icon_offset;
                out->icon_size = mhdr.icon_size;
                out->has_icon = 1;
            } else {
                out->icon_offset = 0;
                out->icon_size = 0;
                out->has_icon = 0;
            }
            out->is_valid = 1;
            return 0;
        }
    }

    uint64_t cnt_offset = 0;
    int cnt_found = 0;

    if (memcmp(hdr, "\x7f" "CNT", 4) == 0) {
        /* Standard PS4 / PS5 CNT at root */
        cnt_offset = 0;
        cnt_found = 1;
    } else if (memcmp(hdr, "\x7f" "FIH", 4) == 0) {
        /* PS5 Package format: check standard entry 2 at offset 0x58 */
        uint64_t cand = read_le64(hdr + 0x58);
        if (cand > 0 && cand < out->file_size) {
            uint8_t test_magic[4];
            if (pread(fd, test_magic, 4, cand) == 4 && memcmp(test_magic, "\x7f" "CNT", 4) == 0) {
                cnt_offset = cand;
                cnt_found = 1;
            }
        }

        /* If not at 0x58, scan table entries in FIH header (64-byte or 32-byte records) */
        if (!cnt_found) {
            for (size_t off = 0x10; off + 8 <= (size_t)hdr_read; off += 0x08) {
                cand = read_le64(hdr + off);
                if (cand >= 0x10000 && cand < out->file_size && (cand % 0x1000) == 0) {
                    uint8_t test_magic[4];
                    if (pread(fd, test_magic, 4, cand) == 4 && memcmp(test_magic, "\x7f" "CNT", 4) == 0) {
                        cnt_offset = cand;
                        cnt_found = 1;
                        break;
                    }
                }
            }
        }
    }

    if (!cnt_found) {
        close(fd);
        return -1;
    }

    /* Read CNT Header */
    uint8_t cnt_hdr[0x80];
    if (pread(fd, cnt_hdr, sizeof(cnt_hdr), cnt_offset) != sizeof(cnt_hdr)) {
        close(fd);
        return -1;
    }

    if (memcmp(cnt_hdr, "\x7f" "CNT", 4) != 0) {
        close(fd);
        return -1;
    }

    uint32_t cnt_type_magic = read_be32(cnt_hdr + 0x04);

    /* Extract Content ID at 0x40 (up to 48 bytes) */
    memcpy(out->content_id, cnt_hdr + 0x40, 48);
    out->content_id[48] = '\0';
    for (int i = 0; i < 48; i++) {
        if ((unsigned char)out->content_id[i] < 32 || (unsigned char)out->content_id[i] > 126) {
            out->content_id[i] = '\0';
            break;
        }
    }

    uint32_t entry_count = read_be32(cnt_hdr + 0x10);
    uint32_t table_offset = read_be32(cnt_hdr + 0x18);

    if (entry_count == 0 || entry_count > 2048 || table_offset > 0x200000) {
        close(fd);
        return -1;
    }

    size_t table_size = (size_t)entry_count * 32;
    /* Pre-check table fits inside the file before malloc+pread, so crafted
     * >4GB offsets can't cause wrong parses or wasted I/O on optical. */
    if (out->file_size > 0 &&
        (uint64_t)cnt_offset + (uint64_t)table_offset + (uint64_t)table_size > out->file_size) {
        close(fd);
        return -1;
    }
    uint8_t *entry_table = (uint8_t *)malloc(table_size);
    if (!entry_table) {
        close(fd);
        return -1;
    }

    if (pread(fd, entry_table, table_size, cnt_offset + table_offset) != (ssize_t)table_size) {
        free(entry_table);
        close(fd);
        return -1;
    }

    /* Locate string table (type 0x0200) */
    uint32_t str_table_off = 0;
    uint32_t str_table_sz = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        const uint8_t *e = entry_table + i * 32;
        uint32_t type = read_be32(e);
        if (type == 0x0200) {
            str_table_off = read_be32(e + 16);
            str_table_sz = read_be32(e + 20);
            break;
        }
    }

    char *str_table = NULL;
    if (str_table_sz > 0 && str_table_sz < 65536) {
        str_table = (char *)malloc(str_table_sz + 1);
        if (str_table) {
            if (pread(fd, str_table, str_table_sz, cnt_offset + str_table_off) == (ssize_t)str_table_sz) {
                str_table[str_table_sz] = '\0';
            } else {
                free(str_table);
                str_table = NULL;
            }
        }
    }

    int has_playgo_chunk_patch = 0;
    int has_delta_patch = 0;

    /* Iterate entries to find param.json, param.sfo, and icon0.png */
    for (uint32_t i = 0; i < entry_count; i++) {
        const uint8_t *e = entry_table + i * 32;
        uint32_t type = read_be32(e);
        uint32_t fn_off = read_be32(e + 4);
        uint32_t data_off = read_be32(e + 16);
        uint32_t data_sz = read_be32(e + 20);

        const char *name = "";
        if (str_table && fn_off < str_table_sz) {
            name = str_table + fn_off;
        }

        if (type == 0x1008 || strcmp(name, "app/playgo-chunk.dat") == 0) {
            has_playgo_chunk_patch = 1;
        }
        if (type == 0x0407 || type == 0x0408 ||
            strcmp(name, "target-deltainfo.dat") == 0 || strcmp(name, "origin-deltainfo.dat") == 0) {
            has_delta_patch = 1;
        }

        /* 1. param.json (PS5) */
        if ((type == 0x2000 || strcmp(name, "param.json") == 0) && data_sz > 0 && data_sz < 262144) {
            char *json_buf = (char *)malloc(data_sz + 1);
            if (json_buf) {
                if (pread(fd, json_buf, data_sz, cnt_offset + data_off) == (ssize_t)data_sz) {
                    json_buf[data_sz] = '\0';
                    char tid[PKG_TITLE_ID_LEN] = {0};
                    char tname[PKG_TITLE_NAME_LEN] = {0};

                    if (json_extract_key(json_buf, data_sz, "titleId", tid, sizeof(tid)) == 0) {
                        strncpy(out->title_id, tid, sizeof(out->title_id) - 1);
                    }
                    if (json_extract_key(json_buf, data_sz, "titleName", tname, sizeof(tname)) == 0) {
                        strncpy(out->title_name, tname, sizeof(out->title_name) - 1);
                    }

                    char cat_buf[16] = {0};
                    if (json_extract_key(json_buf, data_sz, "category", cat_buf, sizeof(cat_buf)) == 0 && out->category[0] == '\0') {
                        strncpy(out->category, cat_buf, sizeof(out->category) - 1);
                    }
                    char ver[32] = {0};
                    if (json_extract_key(json_buf, data_sz, "contentVersion", ver, sizeof(ver)) == 0 ||
                        json_extract_key(json_buf, data_sz, "appVersion", ver, sizeof(ver)) == 0 ||
                        json_extract_key(json_buf, data_sz, "version", ver, sizeof(ver)) == 0) {
                        if (ver[0] != '\0') {
                            int maj = 0, min = 0, patch = 0;
                            if (sscanf(ver, "%d.%d.%d", &maj, &min, &patch) == 3) {
                                if (min == 0 && patch > 0) {
                                    snprintf(out->app_version, sizeof(out->app_version), "v%d.%02d", maj, patch);
                                } else if (patch == 0) {
                                    snprintf(out->app_version, sizeof(out->app_version), "v%d.%02d", maj, min);
                                } else {
                                    snprintf(out->app_version, sizeof(out->app_version), "v%d.%d.%d", maj, min, patch);
                                }
                            } else if (ver[0] != 'v' && ver[0] != 'V') {
                                snprintf(out->app_version, sizeof(out->app_version), "v%.29s", ver);
                            } else {
                                strncpy(out->app_version, ver, sizeof(out->app_version) - 1);
                            }
                        }
                    }
                }
                free(json_buf);
            }
        }

        /* 2. param.sfo (PS4) */
        if ((type == 0x1000 || strcmp(name, "param.sfo") == 0) && data_sz > 0 && data_sz < 262144) {
            uint8_t *sfo_buf = (uint8_t *)malloc(data_sz);
            if (sfo_buf) {
                if (pread(fd, sfo_buf, data_sz, cnt_offset + data_off) == (ssize_t)data_sz) {
                    char stitle[PKG_TITLE_NAME_LEN] = {0};
                    char stid[PKG_TITLE_ID_LEN] = {0};
                    char sver[32] = {0};
                    parse_param_sfo(sfo_buf, data_sz, stitle, sizeof(stitle), stid, sizeof(stid), sver, sizeof(sver),
                                    out->category, sizeof(out->category));
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

        /* 3. icon0.png: cap size so a crafted table can't trigger a ~4GB
         * malloc downstream, and validate the range fits inside the file. */
        if (type == 0x1200 || strcmp(name, "icon0.png") == 0) {
            if (data_sz > 0 && data_sz < 10 * 1024 * 1024 &&
                (out->file_size == 0 ||
                 (uint64_t)cnt_offset + (uint64_t)data_off + (uint64_t)data_sz <= out->file_size)) {
                out->icon_offset = cnt_offset + data_off;
                out->icon_size = data_sz;
                out->has_icon = 1;
            }
        }
    }

    int is_delta_type = ((cnt_type_magic & 0xFF) == 0x1E || (cnt_type_magic & 0xFF000000) == 0x41000000);

    if (has_playgo_chunk_patch || has_delta_patch || is_delta_type ||
        (out->category[0] != '\0' && strncmp(out->category, "gp", 2) == 0)) {
        out->pkg_type = PKG_TYPE_UPDATE;
    } else if (out->category[0] != '\0') {
        if (strncmp(out->category, "ac", 2) == 0 || strncmp(out->category, "al", 2) == 0 ||
            strcmp(out->category, "addcont") == 0) {
            out->pkg_type = PKG_TYPE_DLC;
        } else if (strncmp(out->category, "gd", 2) == 0 || strncmp(out->category, "bd", 2) == 0 ||
                   strncmp(out->category, "gc", 2) == 0 || strncmp(out->category, "wt", 2) == 0) {
            out->pkg_type = PKG_TYPE_BASE;
        }
    } else if (cnt_type_magic == 1) {
        out->pkg_type = PKG_TYPE_DLC;
    }

    if (out->pkg_type == PKG_TYPE_UNKNOWN) {
        out->pkg_type = PKG_TYPE_BASE;
    }

    switch (out->pkg_type) {
        case PKG_TYPE_BASE:
            strncpy(out->pkg_type_str, "base", sizeof(out->pkg_type_str) - 1);
            break;
        case PKG_TYPE_UPDATE:
            strncpy(out->pkg_type_str, "update", sizeof(out->pkg_type_str) - 1);
            break;
        case PKG_TYPE_DLC:
            strncpy(out->pkg_type_str, "dlc", sizeof(out->pkg_type_str) - 1);
            break;
        default:
            strncpy(out->pkg_type_str, "unknown", sizeof(out->pkg_type_str) - 1);
            break;
    }

    if (str_table) {
        free(str_table);
    }
    free(entry_table);
    close(fd);

    /* Fallbacks if title_id or title_name could not be found */
    if (out->title_id[0] == '\0' && out->content_id[0] != '\0') {
        /* Often content_id is XX0000-TITLEID_00-... */
        const char *dash = strchr(out->content_id, '-');
        if (dash) {
            const char *us = strchr(dash + 1, '_');
            if (us && (size_t)(us - (dash + 1)) < sizeof(out->title_id)) {
                size_t len = us - (dash + 1);
                strncpy(out->title_id, dash + 1, len);
                out->title_id[len] = '\0';
            }
        }
    }

    if (out->title_name[0] == '\0') {
        if (out->title_id[0] != '\0') {
            snprintf(out->title_name, sizeof(out->title_name), "%s", out->title_id);
        } else {
            snprintf(out->title_name, sizeof(out->title_name), "Unknown Package");
        }
    }

    out->is_valid = 1;
    return 0;
}

int pkg_parser_get_icon(const char *file_path, uint64_t offset, uint32_t size,
                        uint8_t **out_data, size_t *out_size) {
    if (!file_path || !out_data || !out_size) {
        return -1;
    }

    if (strncmp(file_path, "smb://", 6) == 0) {
        /* When offset/size are already known (e.g. from the scanner cache),
           read directly without re-parsing the entire PKG over SMB. */
        if (offset > 0 && size > 0 && size < 10 * 1024 * 1024) {
            uint8_t *buf = (uint8_t *)malloc(size);
            if (!buf) return -1;
            ssize_t n = smb_client_pread(file_path, buf, size, offset);
            if (n == (ssize_t)size) {
                *out_data = buf;
                *out_size = size;
                return 0;
            }
            free(buf);
        }
        return smb_client_get_icon(file_path, out_data, out_size);
    }

    if (offset == 0 || size == 0 || size >= 10 * 1024 * 1024) {
        return -1;
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct stat st_icon;
    if (fstat(fd, &st_icon) == 0) {
        if (st_icon.st_size <= 0 ||
            offset >= (uint64_t)st_icon.st_size ||
            (uint64_t)size > (uint64_t)st_icon.st_size - offset) {
            close(fd);
            return -1;
        }
    }

    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) {
        close(fd);
        return -1;
    }

    if (pread(fd, data, size, offset) != (ssize_t)size) {
        free(data);
        close(fd);
        return -1;
    }

    close(fd);
    *out_data = data;
    *out_size = size;
    return 0;
}
