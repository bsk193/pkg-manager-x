#!/usr/bin/env python3
"""Rewrite modern CSS Color 4 slash syntax to Safari 12-compatible commas.

Tailwind v3 emits `rgb(R G B / A)` which renders transparent/black on PS5
WebKit (safari12 target). This post-processes the single-file
frontend/dist/index.html after `vite build`:

  rgb(10 10 15 / var(--tw-bg-opacity,1)) -> rgb(10,10,15)
  rgb(59 130 246 / .5)                   -> rgba(59,130,246,.5)
  hsl(220 10% 50% / .5)                  -> hsla(220,10%,50%,.5) (best effort)

Opacity via var() can't be resolved statically, so we fall back to the
opaque color (better than transparent). Numeric alpha is preserved.
Idempotent: already-comma syntax is untouched.
"""
import re
import sys


def fix_css(css: str) -> tuple[str, int]:
    count = 0
    pos = 0
    out = []
    pattern = re.compile(r"\b(rgb|hsl)\(")

    while pos < len(css):
        m = pattern.search(css, pos)
        if not m:
            out.append(css[pos:])
            break

        match_start = m.start()
        fn_name = m.group(1)
        paren_start = m.end() - 1

        # Track balanced parentheses to properly locate the matching ')'
        # even when alpha contains nested parentheses like var(--tw-bg-opacity, 1).
        depth = 0
        paren_end = -1
        for i in range(paren_start, len(css)):
            c = css[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    paren_end = i
                    break

        if paren_end == -1:
            out.append(css[pos : match_start + 4])
            pos = match_start + 4
            continue

        inner = css[paren_start + 1 : paren_end].strip()

        # Check if it uses CSS Color 4 slash syntax: 'channels / alpha'
        if "/" in inner:
            slash_parts = inner.split("/", 1)
            channels_str = slash_parts[0].strip()
            alpha_str = slash_parts[1].strip()

            if fn_name == "rgb":
                ch_m = re.fullmatch(r"(\d{1,3})\s+(\d{1,3})\s+(\d{1,3})", channels_str)
                if ch_m:
                    r, g, b = ch_m.group(1), ch_m.group(2), ch_m.group(3)
                    count += 1
                    out.append(css[pos:match_start])
                    if re.fullmatch(r"[\d.]+%?", alpha_str):
                        out.append(f"rgba({r},{g},{b},{alpha_str})")
                    else:
                        out.append(f"rgb({r},{g},{b})")
                    pos = paren_end + 1
                    continue
            elif fn_name == "hsl":
                ch_m = re.fullmatch(
                    r"([-\d.]+(?:deg|grad|rad|turn)?)\s+([\d.]+%)\s+([\d.]+%?)",
                    channels_str,
                )
                if ch_m:
                    h, s, l = ch_m.group(1), ch_m.group(2), ch_m.group(3)
                    count += 1
                    out.append(css[pos:match_start])
                    if re.fullmatch(r"[\d.]+%?", alpha_str):
                        out.append(f"hsla({h},{s},{l},{alpha_str})")
                    else:
                        out.append(f"hsl({h},{s},{l})")
                    pos = paren_end + 1
                    continue

        out.append(css[pos : paren_end + 1])
        pos = paren_end + 1

    return "".join(out), count


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "frontend/dist/index.html"
    with open(path, "r", encoding="utf-8") as f:
        orig = f.read()
    fixed, n = fix_css(orig)
    if n and fixed != orig:
        with open(path, "w", encoding="utf-8") as f:
            f.write(fixed)
    print(f"legacy-css: rewrote {n} rgb() slash expressions in {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
