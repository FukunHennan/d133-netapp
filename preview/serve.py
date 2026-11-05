"""Local preview server for the device-UI / portal HTML prototypes.

Serves the repository root so that relative asset paths work, and exposes the
preview pages at short URLs:

    http://127.0.0.1:8000/device-ui.html      -> preview/device-ui.html
    http://127.0.0.1:8000/portal-preview.html -> preview/portal-preview.html
    http://127.0.0.1:8000/                    -> preview/device-ui.html

Only binds to 127.0.0.1 by default (not reachable from the LAN).

Usage:
    python preview/serve.py
    python preview/serve.py --port 8080
    python preview/serve.py --host 0.0.0.0 --port 8000   # LAN, use with care
"""

import argparse
import http.server
import os
import socketserver
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

ALIASES = {
    "/": "preview/device-ui.html",
    "/device-ui.html": "preview/device-ui.html",
    "/device-ui": "preview/device-ui.html",
    "/portal-preview.html": "preview/portal-preview.html",
    "/portal-preview": "preview/portal-preview.html",
}


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(REPO), **kwargs)

    def do_GET(self):
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        if path in ALIASES:
            self.path = "/" + ALIASES[path]
        return super().do_GET()

    def do_HEAD(self):
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        if path in ALIASES:
            self.path = "/" + ALIASES[path]
        return super().do_HEAD()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("[preview] %s - %s\n" % (self.address_string(), fmt % args))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer((args.host, args.port), Handler) as httpd:
        print(f"preview server on http://{args.host}:{args.port}/device-ui.html")
        print(f"serving {REPO}")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
