#!/usr/bin/env python3
"""Logging TCP proxy for diagnosing PS5 ShellCore package-fetch requests.

Sits between the PS5 and a real HTTP file server and prints full request
and response headers in both directions (notably the Range headers and the
product=/serverIpAddr=/r= query params ShellCore appends, which stock
server logs do not show).

Usage (on the PC that serves the PKG):
    1. Serve the PKG dir:   python3 -m RangeHTTPServer 9999
    2. Run this proxy:       python3 tools/proxy_log.py  (listens on :9998)
    3. Fresh PKG copy for clean downloader state:  copy test.pkg test2.pkg
    4. Trigger install via the payload API:
           curl -X POST http://<PS5-IP>:8844/api/install ^
             -H "Content-Type: application/json" ^
             -d "{\"path\":\"http://<PC-IP>:9998/test2.pkg\"}"
    5. Paste the complete proxy output plus the curl result.
"""

import socket
import threading

LISTEN_PORT = 9998
TARGET_HOST = "127.0.0.1"
TARGET_PORT = 9999  # your RangeHTTPServer


def handle(client):
    try:
        req = b""
        while b"\r\n\r\n" not in req:
            chunk = client.recv(65536)
            if not chunk:
                client.close()
                return
            req += chunk
            if len(req) > 1048576:
                client.close()
                return
        print("===== REQUEST =====", flush=True)
        print(req.split(b"\r\n\r\n")[0].decode("latin1"), flush=True)
        srv = socket.create_connection((TARGET_HOST, TARGET_PORT), timeout=15)
        srv.sendall(req)
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = srv.recv(65536)
            if not chunk:
                break
            resp += chunk
        if resp:
            print("----- RESPONSE ----", flush=True)
            print(resp.split(b"\r\n\r\n")[0].decode("latin1"), flush=True)
            client.sendall(resp)
        srv.settimeout(30)
        client.settimeout(30)

        def fwd(s, d):
            try:
                while True:
                    b = s.recv(65536)
                    if not b:
                        break
                    d.sendall(b)
            except Exception:
                pass

        t = threading.Thread(target=fwd, args=(srv, client), daemon=True)
        t.start()
        fwd(client, srv)
    except Exception as e:
        print("proxy err:", e, flush=True)
    finally:
        try:
            client.close()
        except Exception:
            pass


def main():
    ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("0.0.0.0", LISTEN_PORT))
    ls.listen(16)
    print(f"proxy :{LISTEN_PORT} -> {TARGET_HOST}:{TARGET_PORT}", flush=True)
    while True:
        c, _ = ls.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


if __name__ == "__main__":
    main()
