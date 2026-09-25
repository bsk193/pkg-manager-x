# HTTP / HTTPS sources

PKG Manager X can install packages straight from a web server. Nothing is copied
to the console first: the console's installer pulls byte ranges from the local
stream server (`127.0.0.1:18841`), which fetches the same ranges from your server.

Add a server in **Settings → Network Sources → HTTP / HTTPS Servers**, press
**Test Connection**, then **Save Server**. It then shows up as its own source on
the start page (and in **All Sources**).

## Requirements

- The server must answer `Range: bytes=a-b` with **206 Partial Content**. Every
  common server does this for static files (nginx, Apache, Caddy, lighttpd, IIS,
  `python -m http.server`, Synology/QNAP web station). The connection test reports
  it when ranges are ignored.
- Packages are found in one of two ways:
  1. **`index.json`** in the base folder (preferred for large libraries: no extra
     request per file). Generate it with:
     ```bash
     python3 tools/make_http_index.py /srv/pkgs
     ```
     Format: `{"files":[{"path":"PS4/Game.pkg","size":123,"mtime":1700000000}, "PS5/Other.pkg"]}`.
     Paths are plain (not URL-encoded) and relative to the base URL. Re-run the
     script after adding packages.
  2. **HTML directory listing** (autoindex). Sub-folders are followed up to 5
     levels, so `PS4/` and `PS5/` folders work as expected. Each package gets one
     `HEAD` request for its size and date; quick background rescans of HTTP sources
     run at most once a minute.
- Split packages (`.part2` ...) are USB/Disc only, same as SMB.

## Server examples

Serve `/srv/pkgs` (with `PS4/` and `PS5/` inside) on port 8080:

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
        autoindex on;          # or drop this and use index.json
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

HTTPS uses mbedTLS (TLS 1.2). Pick how the server certificate is checked:

| Mode | When to use |
|------|-------------|
| **Verify with CA bundle** | Public certificates (Let's Encrypt, ...). Copy a CA bundle such as curl's [`cacert.pem`](https://curl.se/docs/caextract.html) to `/data/pkgmgr/cacert.pem` on the console (FTP). |
| **Trust a specific certificate** | Self-signed NAS / home server. Press **Test Connection**, check the SHA-256 fingerprint, then **Trust this certificate**. The connection is refused if the certificate changes. |
| **Do not check** | Last resort on a trusted LAN. Traffic is encrypted but the server is not authenticated. |

Builds made with `make HTTPS=0` support plain `http://` only.

## Troubleshooting

- **"Access denied (HTTP 401/403)"**: check username/password and server permissions.
- **"server ignores byte-range requests"**: the file is served dynamically (PHP,
  some proxies/CDNs with compression). Serve the folder as static files and
  disable gzip for `.pkg`.
- **Install stalls on slow links**: installs stream from the server in real time;
  Wi-Fi or a VPN to a remote server may be too slow for the console installer.
  Prefer wired LAN.
- Logs: `http://<console-ip>:8844/api/log` (lines tagged `[HTTP]`).
