#!/usr/bin/env python3
"""
PS5 PKG Multi-Part Splitter Tool
Splits large PS5/PS4 .pkg packages into multi-part files
sized for optical media (BD-R, DVD) or storage drives.
"""

import os
import sys
import argparse
import struct
import uuid
import json
import re

MULTIPART_MAGIC = b"PS5MPKG1"
HEADER_SIZE = 4096
DEFAULT_PART_SIZE = 24 * 1000 * 1000 * 1000  # 24 GB (fits safely on BD-R 25GB)
DEFAULT_CHUNK_SIZE = 2 * 1024 * 1024         # 2 MB chunks

def parse_size(size_str):
    s = str(size_str).strip().upper()
    units = {
        'B': 1,
        'K': 1024,
        'KB': 1000,
        'KIB': 1024,
        'M': 1024 * 1024,
        'MB': 1000 * 1000,
        'MIB': 1024 * 1024,
        'G': 1024 * 1024 * 1024,
        'GB': 1000 * 1000 * 1000,
        'GIB': 1024 * 1024 * 1024,
    }
    for unit, mult in sorted(units.items(), key=lambda x: -len(x[0])):
        if s.endswith(unit):
            num = s[:-len(unit)].strip()
            return int(float(num) * mult)
    return int(s)

def format_bytes(b):
    for u in ['B', 'KB', 'MB', 'GB', 'TB']:
        if b < 1024.0:
            return f"{b:.2f} {u}"
        b /= 1024.0
    return f"{b:.2f} PB"

def parse_sfo(data):
    title = ""
    title_id = ""
    app_ver = ""
    category = ""
    if len(data) < 20 or data[0:4] != b"\x00PSF":
        return title, title_id, app_ver, category
    key_table_start, data_table_start, entry_count = struct.unpack_from("<III", data, 0x08)
    for i in range(entry_count):
        off = 20 + i * 16
        if off + 16 > len(data):
            break
        key_off, _, data_len, _, data_off = struct.unpack_from("<HHIII", data, off)
        key_end = data.find(b"\x00", key_table_start + key_off)
        if key_end == -1:
            continue
        key = data[key_table_start + key_off:key_end].decode('utf-8', errors='ignore')
        val_raw = data[data_table_start + data_off:data_table_start + data_off + data_len]
        val = val_raw.rstrip(b"\x00").decode('utf-8', errors='ignore')
        if key == "TITLE" and not title:
            title = val
        elif key == "TITLE_ID" and not title_id:
            title_id = val
        elif (key == "APP_VER" or key == "VERSION") and not app_ver:
            app_ver = val
        elif key == "CATEGORY" and not category:
            category = val
    return title, title_id, app_ver, category

def extract_pkg_metadata(pkg_path):
    info = {
        "title_id": "",
        "title_name": "",
        "content_id": "",
        "app_version": "",
        "pkg_type": "base",
        "icon_data": b"",
        "file_size": os.path.getsize(pkg_path)
    }

    try:
        with open(pkg_path, "rb") as f:
            hdr = f.read(0x200)
            if len(hdr) < 0x80:
                return info

            cnt_offset = 0
            if hdr[0:4] == b"\x7fCNT":
                cnt_offset = 0
            elif hdr[0:4] == b"\x7fFIH":
                cand, = struct.unpack_from("<Q", hdr, 0x58)
                if cand > 0 and cand < info["file_size"]:
                    f.seek(cand)
                    if f.read(4) == b"\x7fCNT":
                        cnt_offset = cand
                if cnt_offset == 0:
                    for off in range(0x10, len(hdr) - 8, 8):
                        c, = struct.unpack_from("<Q", hdr, off)
                        if c >= 0x10000 and c < info["file_size"] and (c % 0x1000) == 0:
                            f.seek(c)
                            if f.read(4) == b"\x7fCNT":
                                cnt_offset = c
                                break

            f.seek(cnt_offset)
            cnt_hdr = f.read(0x80)
            if len(cnt_hdr) < 0x80 or cnt_hdr[0:4] != b"\x7fCNT":
                return info

            raw_cid = cnt_hdr[0x40:0x70]
            null_pos = raw_cid.find(b"\x00")
            if null_pos != -1:
                raw_cid = raw_cid[:null_pos]
            info["content_id"] = raw_cid.decode('ascii', errors='ignore')

            cnt_type, = struct.unpack_from(">I", cnt_hdr, 0x04)
            if cnt_type == 1:
                info["pkg_type"] = "dlc"
            is_delta_type = ((cnt_type & 0xFF) == 0x1E or (cnt_type & 0xFF000000) == 0x41000000)

            entry_count, = struct.unpack_from(">I", cnt_hdr, 0x10)
            table_offset, = struct.unpack_from(">I", cnt_hdr, 0x18)

            if entry_count == 0 or entry_count > 2048 or table_offset > 0x200000:
                return info

            f.seek(cnt_offset + table_offset)
            entry_table = f.read(entry_count * 32)
            if len(entry_table) < entry_count * 32:
                return info

            str_table_off = 0
            str_table_sz = 0
            for i in range(entry_count):
                e = entry_table[i*32:(i+1)*32]
                etype, = struct.unpack_from(">I", e, 0)
                if etype == 0x0200:
                    str_table_off, str_table_sz = struct.unpack_from(">II", e, 16)
                    break

            str_table = b""
            if str_table_sz > 0 and str_table_sz < 65536:
                f.seek(cnt_offset + str_table_off)
                str_table = f.read(str_table_sz)

            has_patch_chunk = False
            has_delta_patch = is_delta_type

            for i in range(entry_count):
                e = entry_table[i*32:(i+1)*32]
                etype, fn_off = struct.unpack_from(">II", e, 0)
                data_off, data_sz = struct.unpack_from(">II", e, 16)

                name = ""
                if str_table and fn_off < len(str_table):
                    null_pos = str_table.find(b"\x00", fn_off)
                    if null_pos != -1:
                        name = str_table[fn_off:null_pos].decode('ascii', errors='ignore')

                if etype == 0x1008 or name == "app/playgo-chunk.dat":
                    has_patch_chunk = True
                if etype in (0x0407, 0x0408) or name in ("target-deltainfo.dat", "origin-deltainfo.dat"):
                    has_delta_patch = True

                if (etype == 0x2000 or name == "param.json") and 0 < data_sz < 262144:
                    f.seek(cnt_offset + data_off)
                    jbuf = f.read(data_sz).decode('utf-8', errors='ignore')
                    m_tid = re.search(r'"titleId"\s*:\s*"([^"]+)"', jbuf)
                    if m_tid:
                        info["title_id"] = m_tid.group(1)
                    m_tname = re.search(r'"titleName"\s*:\s*"([^"]+)"', jbuf)
                    if m_tname:
                        info["title_name"] = m_tname.group(1)
                    m_ver = re.search(r'"(contentVersion|appVersion|version)"\s*:\s*"([^"]+)"', jbuf)
                    if m_ver:
                        info["app_version"] = m_ver.group(2)
                    m_cat = re.search(r'"category"\s*:\s*"([^"]+)"', jbuf)
                    if m_cat:
                        c = m_cat.group(1).lower()
                        if c in ("ac", "al", "addcont") or c.startswith("ac") or c.startswith("al"):
                            info["pkg_type"] = "dlc"
                        elif c.startswith("gp"):
                            info["pkg_type"] = "update"
                        elif c.startswith("gd") or c.startswith("bd") or c.startswith("gc") or c.startswith("wt"):
                            info["pkg_type"] = "base"

                if (etype == 0x1000 or name == "param.sfo") and 0 < data_sz < 262144:
                    f.seek(cnt_offset + data_off)
                    sfo_data = f.read(data_sz)
                    s_title, s_id, s_ver, s_cat = parse_sfo(sfo_data)
                    if not info["title_name"] and s_title:
                        info["title_name"] = s_title
                    if not info["title_id"] and s_id:
                        info["title_id"] = s_id
                    if not info["app_version"] and s_ver:
                        info["app_version"] = s_ver
                    if s_cat:
                        sc = s_cat.lower()
                        if sc.startswith("ac") or sc.startswith("al"):
                            info["pkg_type"] = "dlc"
                        elif sc.startswith("gp"):
                            info["pkg_type"] = "update"
                        elif sc.startswith("gd") or sc.startswith("bd") or sc.startswith("wt"):
                            info["pkg_type"] = "base"

                if (etype == 0x1200 or name == "icon0.png") and 0 < data_sz < 1048576:
                    f.seek(cnt_offset + data_off)
                    info["icon_data"] = f.read(data_sz)

            if has_patch_chunk or has_delta_patch:
                info["pkg_type"] = "update"

    except Exception as ex:
        sys.stderr.write(f"Notice: PKG metadata inspection: {ex}\n")

    if not info["title_name"]:
        info["title_name"] = info["title_id"] if info["title_id"] else "Unknown Package"
    if not info["title_id"]:
        info["title_id"] = "UNKNOWN"

    return info

def pack_header(part_index, total_parts, chunk_size, num_chunks,
                part_data_size, total_pkg_size, total_archive_size, pkg_filename,
                title_id, title_name, content_id, pkg_uuid, icon_offset, icon_size,
                app_version="", pkg_type="base", part_offset=0, data_offset=4096):
    return struct.pack(
        "<8sIIIIIIQQQ256s32s256s64s16sII32s16sQI3348s",
        MULTIPART_MAGIC,
        1,                      # header_version
        part_index,
        total_parts,
        0,                      # compression_type (0 = raw slice)
        chunk_size,
        num_chunks,             # 0 for raw slice
        part_data_size,
        total_pkg_size,
        total_archive_size,
        pkg_filename.encode('utf-8')[:255].ljust(256, b'\x00'),
        title_id.encode('utf-8')[:31].ljust(32, b'\x00'),
        title_name.encode('utf-8')[:255].ljust(256, b'\x00'),
        content_id.encode('utf-8')[:63].ljust(64, b'\x00'),
        pkg_uuid,
        icon_offset,
        icon_size,
        app_version.encode('utf-8')[:31].ljust(32, b'\x00'),
        pkg_type.encode('utf-8')[:15].ljust(16, b'\x00'),
        part_offset,
        data_offset,
        b"\x00" * 3348
    )

def split_pkg(pkg_path, output_dir, part_size=DEFAULT_PART_SIZE,
              chunk_size=DEFAULT_CHUNK_SIZE):
    if not os.path.isfile(pkg_path):
        raise FileNotFoundError(f"Input file not found: {pkg_path}")

    os.makedirs(output_dir, exist_ok=True)

    filename = os.path.basename(pkg_path)
    total_pkg_size = os.path.getsize(pkg_path)

    print(f"--- PS5 PKG Multi-Part Splitter Tool ---")
    print(f"Input Package : {pkg_path}")
    print(f"Total Size    : {format_bytes(total_pkg_size)} ({total_pkg_size:,} bytes)")
    print(f"Part Limit    : {format_bytes(part_size)} ({part_size:,} bytes)")
    print(f"Format        : Sequential package slices")
    print("Reading package metadata...")

    meta = extract_pkg_metadata(pkg_path)
    print(f"  Title ID    : {meta['title_id']}")
    print(f"  Title Name  : {meta['title_name']}")
    print(f"  Content ID  : {meta['content_id']}")
    has_icon = len(meta['icon_data']) > 0
    print(f"  Icon Embed  : {'Yes (' + format_bytes(len(meta['icon_data'])) + ')' if has_icon else 'No'}")

    pkg_uuid = uuid.uuid4().bytes

    parts_info = []
    current_part = 1
    bytes_read_total = 0
    total_archive_size = 0

    print("\nSplitting package into sequential slices...")

    io_buffer_size = 2 * 1024 * 1024  # 2MB I/O read buffer

    with open(pkg_path, "rb") as in_f:
        while bytes_read_total < total_pkg_size or current_part == 1:
            current_part_path = os.path.join(output_dir, f"{filename}.part{current_part}")
            current_file = open(current_part_path, "wb")

            # Reserve 4096 bytes for header
            current_file.write(b"\x00" * HEADER_SIZE)

            # In Part 1, embed icon0.png immediately after header if available
            icon_offset = 0
            icon_size = 0
            if has_icon and current_part == 1:
                icon_offset = HEADER_SIZE
                icon_size = len(meta['icon_data'])
                current_file.write(meta['icon_data'])

            data_offset = current_file.tell()
            part_offset = bytes_read_total
            max_part_payload = max(1, part_size - data_offset)

            current_part_data_size = 0

            while current_part_data_size < max_part_payload:
                to_read = min(io_buffer_size, max_part_payload - current_part_data_size)
                chunk = in_f.read(to_read)
                if not chunk:
                    break
                current_file.write(chunk)
                current_part_data_size += len(chunk)
                bytes_read_total += len(chunk)

                if bytes_read_total % (50 * 1024 * 1024) < io_buffer_size or bytes_read_total == total_pkg_size:
                    pct = (bytes_read_total / total_pkg_size) * 100.0 if total_pkg_size > 0 else 100.0
                    sys.stdout.write(f"\r  Progress: {pct:5.1f}% ({format_bytes(bytes_read_total)} / {format_bytes(total_pkg_size)})")
                    sys.stdout.flush()

            current_file.close()
            part_fsize = os.path.getsize(current_part_path)
            total_archive_size += part_fsize

            parts_info.append({
                "index": current_part,
                "path": current_part_path,
                "num_chunks": 0,
                "part_data_size": current_part_data_size,
                "part_offset": part_offset,
                "data_offset": data_offset,
                "file_size": part_fsize,
                "icon_offset": icon_offset,
                "icon_size": icon_size
            })

            print(f"\n  [Disc {current_part}] Created {os.path.basename(current_part_path)}: "
                  f"{format_bytes(part_fsize)} ({format_bytes(current_part_data_size)} payload, offset {part_offset})")

            current_part += 1

            if bytes_read_total >= total_pkg_size:
                break

    total_parts = len(parts_info)

    # Rewrite finalized headers for all parts
    print("\nWriting container headers across all parts...")
    for p in parts_info:
        hdr_bytes = pack_header(
            part_index=p["index"],
            total_parts=total_parts,
            chunk_size=io_buffer_size,
            num_chunks=0,
            part_data_size=p["part_data_size"],
            total_pkg_size=total_pkg_size,
            total_archive_size=total_archive_size,
            pkg_filename=filename,
            title_id=meta["title_id"],
            title_name=meta["title_name"],
            content_id=meta["content_id"],
            pkg_uuid=pkg_uuid,
            icon_offset=p["icon_offset"],
            icon_size=p["icon_size"],
            app_version=meta.get("app_version", ""),
            pkg_type=meta.get("pkg_type", "base"),
            part_offset=p["part_offset"],
            data_offset=p["data_offset"]
        )
        with open(p["path"], "r+b") as f:
            f.seek(0)
            f.write(hdr_bytes)

    ratio = (total_archive_size / total_pkg_size) * 100.0 if total_pkg_size > 0 else 100.0
    print("\n=== Multi-Part Disc Splitting Complete! ===")
    print(f"Original PKG Size : {format_bytes(total_pkg_size)}")
    print(f"Total Slices Size : {format_bytes(total_archive_size)} ({ratio:.1f}% of original)")
    print(f"Total Discs/Parts : {total_parts}")
    for p in parts_info:
        print(f"  Disc {p['index']}: {os.path.basename(p['path'])} -> {format_bytes(p['file_size'])}")

    print("\n=== Optical Disc Layout Instructions ===")
    for p in parts_info:
        print(f"  Burn to Disc {p['index']}: Place '{os.path.basename(p['path'])}' into /pkg/ directory on disc {p['index']}.")
    print("The PKG Manager payload will automatically detect each disc upon insertion.")

    return parts_info

def main():
    parser = argparse.ArgumentParser(
        description="PS5 PKG Multi-Part Splitter Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("input_pkg", help="Path to input .pkg file")
    parser.add_argument("-o", "--output-dir", default=None,
                        help="Output directory (default: ./<pkg_name>_parts)")
    parser.add_argument("-s", "--part-size", default="23G",
                        help="Maximum size per part (e.g. 23G, 24GB, 46G, 4G, 100M). Default: 23G")
    parser.add_argument("--chunk-size", default="2M",
                        help="Nominal chunk size (default: 2M)")

    args = parser.parse_args()

    pkg_path = os.path.abspath(args.input_pkg)
    if not os.path.exists(pkg_path):
        print(f"Error: input file '{pkg_path}' not found.", file=sys.stderr)
        sys.exit(1)

    part_size_bytes = parse_size(args.part_size)
    chunk_size_bytes = parse_size(args.chunk_size)

    if args.output_dir:
        out_dir = os.path.abspath(args.output_dir)
    else:
        base_name = os.path.splitext(os.path.basename(pkg_path))[0]
        out_dir = os.path.abspath(f"{base_name}_parts")

    try:
        split_pkg(
            pkg_path=pkg_path,
            output_dir=out_dir,
            part_size=part_size_bytes,
            chunk_size=chunk_size_bytes
        )
    except Exception as e:
        print(f"\nError: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
