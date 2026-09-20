/*
 * Shared synthetic PKG fixture generator for host tests.
 *
 * Builds minimal but fully valid packages parseable by pkg_parser,
 * pkg_scanner, pkg_cache (SMB), installer, multipart and
 * tools/pkg_split.py — so no test needs real files from
 * .for_reference/. All IDs/titles are fictional (CUSA/PPSA 9xxxx).
 *
 * Layouts mirror the structures pkg_parser.c understands:
 *  - PS4: raw "\x7fCNT" header + string table + param.sfo (+ icon0.png)
 *  - PS5: "\x7fFIH" header with CNT offset at 0x58 + CNT + param.json
 */

#ifndef TEST_FIXTURE_H
#define TEST_FIXTURE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fake icon payload: real PNG magic + zero filler. */
#define FIXTURE_ICON_SIZE 1072
static const uint8_t kFixturePngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

static inline void fx_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline void fx_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (i * 8));
    }
}

static inline void fx_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void fx_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Builds a param.sfo with TITLE/TITLE_ID/CATEGORY/APP_VER. Returns size, 0 on overflow. */
static inline size_t fixture_build_sfo(uint8_t *out, size_t cap,
                                const char *title, const char *title_id,
                                const char *category, const char *app_ver) {
    static const char *keys[] = {"TITLE", "TITLE_ID", "CATEGORY", "APP_VER"};
    const char *vals[4];
    vals[0] = title ? title : "";
    vals[1] = title_id ? title_id : "";
    vals[2] = category ? category : "gd";
    vals[3] = app_ver ? app_ver : "01.00";

    size_t key_off[4];
    size_t key_total = 0;
    for (int i = 0; i < 4; i++) {
        key_off[i] = key_total;
        key_total += strlen(keys[i]) + 1;
    }
    size_t data_off[4];
    size_t data_total = 0;
    size_t data_len[4];
    for (int i = 0; i < 4; i++) {
        data_off[i] = data_total;
        data_len[i] = strlen(vals[i]) + 1;
        data_total += data_len[i];
    }

    size_t key_start = 20 + 4 * 16;
    size_t data_start = key_start + key_total;
    size_t total = data_start + data_total;
    if (total > cap) {
        return 0;
    }

    memset(out, 0, total);
    memcpy(out, "\x00PSF", 4);
    out[4] = 1;
    out[5] = 1;
    fx_le32(out + 8, (uint32_t)key_start);
    fx_le32(out + 12, (uint32_t)data_start);
    fx_le32(out + 16, 4);
    for (int i = 0; i < 4; i++) {
        uint8_t *e = out + 20 + (size_t)i * 16;
        fx_le16(e + 0, (uint16_t)key_off[i]);
        e[2] = 0x04;
        e[3] = 0x02; /* utf-8 string */
        fx_le32(e + 4, (uint32_t)data_len[i]);
        fx_le32(e + 8, (uint32_t)data_len[i]);
        fx_le32(e + 12, (uint32_t)data_off[i]);
    }
    for (int i = 0; i < 4; i++) {
        memcpy(out + key_start + key_off[i], keys[i], strlen(keys[i]) + 1);
        memcpy(out + data_start + data_off[i], vals[i], data_len[i]);
    }
    return total;
}

/* Builds a PS5 param.json. Returns size, 0 on overflow. */
static inline size_t fixture_build_json(char *out, size_t cap,
                                 const char *title_id, const char *title,
                                 const char *category, const char *content_ver) {
    int n = snprintf(out, cap,
                     "{\"titleId\":\"%s\",\"titleName\":\"%s\","
                     "\"category\":\"%s\",\"contentVersion\":\"%s\"}",
                     title_id ? title_id : "",
                     title ? title : "",
                     category ? category : "gd",
                     content_ver ? content_ver : "01.000.000");
    if (n <= 0 || (size_t)n >= cap) {
        return 0;
    }
    return (size_t)n;
}

static inline size_t fixture_build_multilang_json(char *out, size_t cap,
                                                  const char *title_id, const char *default_lang,
                                                  const char *category, const char *content_ver) {
    int n = snprintf(out, cap,
                     "{\"titleId\":\"%s\","
                     "\"category\":\"%s\",\"contentVersion\":\"%s\","
                     "\"localizedParameters\":{"
                     "\"ar-AE\":{\"titleName\":\"Arabic Title\"},"
                     "\"defaultLanguage\":\"%s\","
                     "\"en-US\":{\"titleName\":\"English Title\"},"
                     "\"pl-PL\":{\"titleName\":\"Polish Title\"}"
                     "}}",
                     title_id ? title_id : "",
                     category ? category : "gd",
                     content_ver ? content_ver : "01.000.000",
                     default_lang ? default_lang : "en-US");
    if (n <= 0 || (size_t)n >= cap) {
        return 0;
    }
    return (size_t)n;
}

typedef struct {
    const char *title_id;
    const char *title;
    const char *category;   /* "gd" base, "gp" update, "ac" dlc */
    const char *version;    /* SFO "01.00" style or JSON "01.000.000" style */
    int with_icon;
    const char *content_id; /* NULL => derived EP0001-<tid>_00-TEST000000000001 */
    int is_multilang;
    const char *default_lang;
} fixture_pkg_spec_t;

/* Writes one CNT-based package. is_ps5 selects FIH+CNT vs raw CNT.
 * Returns 0 on success. */
static inline int fixture_write_pkg(const char *path, int is_ps5, const fixture_pkg_spec_t *spec) {
    if (!path || !spec || !spec->title_id) {
        return -1;
    }

    uint8_t payload[8192];
    size_t payload_len = 0;
    const char *payload_name = NULL;
    if (is_ps5) {
        payload_name = "param.json";
        if (spec->is_multilang) {
            payload_len = fixture_build_multilang_json((char *)payload, sizeof(payload),
                                                       spec->title_id, spec->default_lang,
                                                       spec->category, spec->version);
        } else {
            payload_len = fixture_build_json((char *)payload, sizeof(payload),
                                             spec->title_id, spec->title,
                                             spec->category, spec->version);
        }
    } else {
        payload_name = "param.sfo";
        payload_len = fixture_build_sfo(payload, sizeof(payload),
                                        spec->title, spec->title_id,
                                        spec->category, spec->version);
    }
    if (payload_len == 0) {
        return -1;
    }

    /* String table: payload name + optional icon name. */
    char strtab[64];
    size_t icon_name_off = 0;
    size_t strtab_len = strlen(payload_name) + 1;
    memcpy(strtab, payload_name, strtab_len);
    if (spec->with_icon) {
        icon_name_off = strtab_len;
        memcpy(strtab + strtab_len, "icon0.png", 10);
        strtab_len += 10;
    }

    int n_entries = spec->with_icon ? 3 : 2;
    size_t table_size = (size_t)n_entries * 32;
    size_t data_base = 0x80 + table_size;
    size_t strtab_off = data_base;
    size_t payload_off = strtab_off + strtab_len;
    size_t icon_off = payload_off + payload_len;

    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }

    uint64_t cnt_offset = 0;
    if (is_ps5) {
        /* FIH header with CNT offset at 0x58 (0x1000-aligned). */
        uint8_t fih[0x200];
        memset(fih, 0, sizeof(fih));
        memcpy(fih, "\x7f" "FIH", 4);
        cnt_offset = 0x10000;
        fx_le64(fih + 0x58, cnt_offset);
        if (fwrite(fih, 1, sizeof(fih), f) != sizeof(fih)) {
            fclose(f);
            return -1;
        }
        /* Pad up to the CNT offset. */
        uint8_t zero[4096];
        memset(zero, 0, sizeof(zero));
        size_t pad_left = (size_t)cnt_offset - sizeof(fih);
        while (pad_left > 0) {
            size_t w = pad_left > sizeof(zero) ? sizeof(zero) : pad_left;
            if (fwrite(zero, 1, w, f) != w) {
                fclose(f);
                return -1;
            }
            pad_left -= w;
        }
    }

    /* CNT header. */
    uint8_t cnt[0x80];
    memset(cnt, 0, sizeof(cnt));
    memcpy(cnt, "\x7f" "CNT", 4);
    fx_be32(cnt + 0x10, (uint32_t)n_entries);
    fx_be32(cnt + 0x18, 0x80);
    {
        char cid[49];
        if (spec->content_id && spec->content_id[0] != '\0') {
            strncpy(cid, spec->content_id, sizeof(cid) - 1);
            cid[sizeof(cid) - 1] = '\0';
        } else {
            snprintf(cid, sizeof(cid), "EP0001-%.9s_00-TEST000000000001", spec->title_id);
        }
        memcpy(cnt + 0x40, cid, strlen(cid));
    }
    if (fwrite(cnt, 1, sizeof(cnt), f) != sizeof(cnt)) {
        fclose(f);
        return -1;
    }

    /* Entry table: [strtab][payload][icon?]. Offsets are CNT-relative. */
    for (int i = 0; i < n_entries; i++) {
        uint8_t e[32];
        memset(e, 0, sizeof(e));
        if (i == 0) {
            fx_be32(e + 0, 0x0200);
            fx_be32(e + 4, 0);
            fx_be32(e + 16, (uint32_t)strtab_off);
            fx_be32(e + 20, (uint32_t)strtab_len);
        } else if (i == 1) {
            fx_be32(e + 0, is_ps5 ? 0x2000 : 0x1000);
            fx_be32(e + 4, 0);
            fx_be32(e + 16, (uint32_t)payload_off);
            fx_be32(e + 20, (uint32_t)payload_len);
        } else {
            fx_be32(e + 0, 0x1200);
            fx_be32(e + 4, (uint32_t)icon_name_off);
            fx_be32(e + 16, (uint32_t)icon_off);
            fx_be32(e + 20, FIXTURE_ICON_SIZE);
        }
        if (fwrite(e, 1, sizeof(e), f) != sizeof(e)) {
            fclose(f);
            return -1;
        }
    }

    if (fwrite(strtab, 1, strtab_len, f) != strtab_len) {
        fclose(f);
        return -1;
    }
    if (fwrite(payload, 1, payload_len, f) != payload_len) {
        fclose(f);
        return -1;
    }
    if (spec->with_icon) {
        uint8_t icon[FIXTURE_ICON_SIZE];
        memset(icon, 0, sizeof(icon));
        memcpy(icon, kFixturePngMagic, sizeof(kFixturePngMagic));
        if (fwrite(icon, 1, sizeof(icon), f) != sizeof(icon)) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

static inline int fixture_write_ps4_pkg(const char *path, const char *title_id,
                                 const char *title, const char *category,
                                 const char *app_ver) {
    fixture_pkg_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.title_id = title_id;
    spec.title = title;
    spec.category = category;
    spec.version = app_ver;
    spec.with_icon = 0;
    return fixture_write_pkg(path, 0, &spec);
}

static inline int fixture_write_ps5_pkg(const char *path, const char *title_id,
                                 const char *title, const char *category,
                                 const char *content_ver, int with_icon) {
    fixture_pkg_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.title_id = title_id;
    spec.title = title;
    spec.category = category;
    spec.version = content_ver;
    spec.with_icon = with_icon;
    return fixture_write_pkg(path, 1, &spec);
}

static inline int fixture_write_ps5_pkg_multilang(const char *path, const char *title_id,
                                                  const char *default_lang, const char *category,
                                                  const char *content_ver, int with_icon) {
    fixture_pkg_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.title_id = title_id;
    spec.default_lang = default_lang;
    spec.category = category;
    spec.version = content_ver;
    spec.with_icon = with_icon;
    spec.is_multilang = 1;
    return fixture_write_pkg(path, 1, &spec);
}

/* Grows a file to target_size with deterministic filler. Returns 0 on success. */
static inline int fixture_grow_file(const char *path, uint64_t target_size) {
    FILE *f = fopen(path, "r+b");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long cur = ftell(f);
    if (cur < 0) {
        fclose(f);
        return -1;
    }
    static uint8_t blk[1024 * 1024];
    static int blk_init = 0;
    if (!blk_init) {
        for (size_t i = 0; i < sizeof(blk); i++) {
            blk[i] = (uint8_t)((i * 31 + 7) & 0xFF);
        }
        blk_init = 1;
    }
    uint64_t left = (target_size > (uint64_t)cur) ? (target_size - (uint64_t)cur) : 0;
    while (left > 0) {
        size_t w = left > sizeof(blk) ? sizeof(blk) : (size_t)left;
        if (fwrite(blk, 1, w, f) != w) {
            fclose(f);
            return -1;
        }
        left -= w;
    }
    fclose(f);
    return 0;
}

#endif /* TEST_FIXTURE_H */
