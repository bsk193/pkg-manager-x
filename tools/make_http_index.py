#!/usr/bin/env python3
"""Generate index.json for a PKG Manager X HTTP source.

PKG Manager X discovers packages on an HTTP(S) source from <base>/index.json
when present, otherwise from the server's HTML directory listing. Use this
script for servers without directory listings (static hosting, object
storage, CDNs) or to skip per-file HEAD requests on large libraries.

    python3 tools/make_http_index.py /srv/pkgs            # writes /srv/pkgs/index.json
    python3 tools/make_http_index.py /srv/pkgs -o -       # print to stdout

Paths are written relative to the folder, unencoded, with forward slashes,
e.g. {"path": "PS4/My Game.pkg", "size": 123, "mtime": 1700000000}.
Only .pkg files are listed; split parts (.part2 ...) are skipped because
multi-part packages are supported on USB/Disc only.
"""

import argparse
import json
import os
import re
import sys

PART_RE = re.compile(r"\.part\d+(\.pkg)?$", re.IGNORECASE)


def collect(root):
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        for name in sorted(filenames):
            if name.startswith(".") or not name.lower().endswith(".pkg") or PART_RE.search(name):
                continue
            full = os.path.join(dirpath, name)
            st = os.stat(full)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            files.append({"path": rel, "size": st.st_size, "mtime": int(st.st_mtime)})
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", help="folder served as the HTTP source base URL")
    ap.add_argument("-o", "--output", help="output file (default: <root>/index.json, '-' for stdout)")
    args = ap.parse_args()

    if not os.path.isdir(args.root):
        ap.error(f"not a directory: {args.root}")

    doc = {"version": 1, "files": collect(args.root)}
    text = json.dumps(doc, indent=2, ensure_ascii=False) + "\n"

    out = args.output or os.path.join(args.root, "index.json")
    if out == "-":
        sys.stdout.write(text)
    else:
        tmp = out + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, out)
        print(f"Wrote {len(doc['files'])} package(s) to {out}")


if __name__ == "__main__":
    main()
