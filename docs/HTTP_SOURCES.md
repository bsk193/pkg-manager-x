# HTTP / HTTPS sources

PKG Manager X can install packages straight from a web server. Nothing is copied
to the console first: the console's installer pulls byte ranges from the local
stream server (`127.0.0.1:18841`), which fetches the same ranges from your server.

Add a server in **Settings → Network Sources → HTTP / HTTPS Servers**, press
**Test Connection**, then **Save Server**. It then shows up as its own source on
the start page (and in **All Sources**). One source is one address: everything
below it (`ps4/`, `ps5/`, `games/`, `dlcs/`, `updates/`, `homebrew/`, ...) is found
automatically.

Where a package is stored never decides what it is. Console (PS4 / PS5) and type
(game, DLC, update, homebrew) always come from the package metadata, so a DLC in
`games/` is still shown as DLC and a PS5 package in `ps4/` is still PS5.

## Requirements

- The server must answer `Range: bytes=a-b` with **206 Partial Content**. Every
  common server does this for static files (nginx, Apache, Caddy, lighttpd, IIS,
  `python -m http.server`, Synology/QNAP web station). The connection test reports
  it when ranges are ignored.
- Packages are found in the first way that works:
  1. **Gateway catalog**: `GET <base>api/catalog` (see below).
  2. **`index.json`** in the base folder. Generate it with:
     ```bash
     python3 tools/make_http_index.py /srv/pkgs
     ```
     Format: `{"files":[{"path":"PS4/Game.pkg","size":123,"mtime":1700000000}, "PS5/Other.pkg"]}`.
     Paths are plain (not URL-encoded) and relative to the base URL. Re-run the
     script after adding packages.
  3. **Directory listing**. With nginx `autoindex_format json;` sizes and dates
     come with the listing. Any HTML index page works too; each package then
     gets one `HEAD` request for its size and date. Sub-folders are followed up
     to 5 levels. Quick background rescans of HTTP sources run at most once a
     minute.
- Split packages (`.part2` ...) are USB/Disc only, same as SMB.

## Home-server gateway

A gateway decides per file whether it is served from local disk or from
Cloudflare R2. The app only needs the one address:

- `GET <base>api/catalog`
  ```json
  {"generated":"2026-09-25T10:00:00Z",
   "files":[{"path":"ps5/games/X.pkg","console":"ps5","size":123,"local":true,"r2":true}]}
  ```
  `size` is checked against the size the server actually delivers; a file whose
  size differs is refused. A file with `"local":false,"r2":false` is listed as
  **unavailable**. `console` is ignored (the package itself says which console
  it is for).
- `GET <base>files/<path>` answers `200` (Range supported), a `302` to a signed
  R2 URL (`*.r2.cloudflarestorage.com`, valid 12 h, Range supported), or `404`.
  - The redirect is followed **without** the source's username/password; Basic
    auth only ever goes to the source's own host.
  - When a signed link expires during an install (R2 answers `403`), the app asks
    `files/<path>` again for a fresh link and continues at the same byte offset.
    Network drops during an install are retried for up to two minutes.
  - A `404` marks the package **unavailable** in the library instead of showing
    an install error.

## Server examples

Serve `/srv/pkgs` (with `ps4/` and `ps5/` inside) on port 8080:

```bash
# Python (quick test, single-threaded before 3.7; fine on a LAN)
cd /srv/pkgs && python3 -m http.server 8080
```

```nginx
# nginx
server {
    listen 8080;
    location /pkgs/ {
        alias /srv/pkgs/;
        autoindex on;
        autoindex_format json;   # sizes/dates in the listing: fastest scans
    }
}
```

```caddyfile
# Caddy
:8080 {
    root * /srv/pkgs
    file_server browse
}
```

Base URL to enter in the app: `http://<server-ip>:8080/` (python / Caddy) or
`http://<server-ip>:8080/pkgs/` (nginx example).

## Authentication

Username/password are sent as HTTP Basic auth, and only to the host of the
source (never to a different host after a redirect). Use HTTPS if the network is
not trusted. Passwords are stored on the console in
`/data/pkgmgr/http_sources.json` and are never sent back to the browser.

## HTTPS

HTTPS uses mbedTLS (TLS 1.2) inside the payload, on both PS4 and PS5. It does
not use the console's own SSL library: that trust store is not reachable from
the payload and firmware root lists lag behind (Let's Encrypt's newer roots,
for example). Instead the **Mozilla CA list** (as published by curl,
`assets/cacert.pem`) is compiled in, so Let's Encrypt, Google Trust Services,
Cloudflare R2 and every other public CA work without copying anything to the
console. Refresh it with `tools/update_ca_bundle.sh`.

Pick how the source's own certificate is checked:

| Mode | When to use |
|------|-------------|
| **Verify** (default) | Public certificates (Let's Encrypt, ...). To also trust your own CA, put its PEM certificate(s) in `/data/pkgmgr/cacert.pem` (or set `PKG_CA_BUNDLE`). |
| **Trust a specific certificate** | Self-signed NAS / home server. Press **Test Connection**, check the SHA-256 fingerprint, then **Trust this certificate**. The connection is refused if the certificate changes. |
| **Do not check** | Last resort on a trusted LAN. Traffic is encrypted but the server is not authenticated. |

These modes apply to the source's own host only. Redirect targets (such as
signed R2 links) are always verified against the CA list.

Builds made with `make HTTPS=0` support plain `http://` only.

## Troubleshooting

- **"Access denied (HTTP 401/403)"**: check username/password and server permissions.
- **"server ignores byte-range requests"**: the file is served dynamically (PHP,
  some proxies/CDNs with compression). Serve the folder as static files and
  disable gzip for `.pkg`.
- **"Certificate not trusted"**: the console clock may be wrong (certificates
  look expired or not yet valid), or the server uses a private CA (see above).
- **Install stalls on slow links**: installs stream from the server in real time;
  Wi-Fi or a VPN to a remote server may be too slow for the console installer.
  Prefer wired LAN.
- Logs: `http://<console-ip>:8844/api/log` (lines tagged `[HTTP]`).
