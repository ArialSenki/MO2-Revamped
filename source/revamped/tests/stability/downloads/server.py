"""A local download server for the download stress run (test only).

    py -3.12 -I server.py <folder with archives> <port file>

127.0.0.1 only. /file/<name>?rate=<bytes per s>&cd=<name for Content-Disposition>
serves a file slowly, with Range (206) for resumes; /cut/<name>?after=<bytes>
announces the whole length and closes after that many bytes. Logs requests
to <port file>.log.
"""
import os
import sys
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = sys.argv[1]
PORT_FILE = sys.argv[2]
LOG = open(PORT_FILE + ".log", "a", encoding="utf-8", buffering=1)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        LOG.write(f"{time.strftime('%H:%M:%S')} {self.command} {self.path} {fmt % args}\n")

    def do_HEAD(self):
        self.do_GET(head=True)

    def do_GET(self, head=False):
        url = urllib.parse.urlsplit(self.path)
        query = dict(urllib.parse.parse_qsl(url.query))
        parts = url.path.split("/", 2)
        if len(parts) < 3 or parts[1] not in ("file", "cut"):
            self.send_error(404)
            return
        name = urllib.parse.unquote(parts[2])
        path = os.path.join(ROOT, name)
        if not os.path.isfile(path) or os.path.dirname(os.path.abspath(path)) != os.path.abspath(ROOT):
            self.send_error(404)
            return
        size = os.path.getsize(path)
        start, end = 0, size - 1
        status = 200
        rng = self.headers.get("Range", "")
        if rng.startswith("bytes="):
            a, _, b = rng[6:].partition("-")
            start = int(a) if a else 0
            end = int(b) if b else size - 1
            if start >= size:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            status = 206
        self.send_response(status)
        self.send_header("Content-Type", "application/zip")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        cd = query.get("cd")
        if cd:
            ascii_name = cd.encode("ascii", "replace").decode().replace("?", "_")
            self.send_header("Content-Disposition",
                             f"attachment; filename=\"{ascii_name}\"; "
                             f"filename*=UTF-8''{urllib.parse.quote(cd)}")
        self.end_headers()
        if head:
            return
        rate = int(query.get("rate", "0"))
        after = int(query.get("after", "-1")) if parts[1] == "cut" else -1
        sent = 0
        chunk = 16 * 1024
        try:
            with open(path, "rb") as f:
                f.seek(start)
                left = end - start + 1
                while left > 0:
                    data = f.read(min(chunk, left))
                    if not data:
                        break
                    if after >= 0 and sent + len(data) > after:
                        data = data[: max(0, after - sent)]
                        self.wfile.write(data)
                        self.wfile.flush()
                        LOG.write(f"{time.strftime('%H:%M:%S')} cut {name} after {after}\n")
                        self.close_connection = True
                        return
                    self.wfile.write(data)
                    sent += len(data)
                    left -= len(data)
                    if rate:
                        time.sleep(len(data) / rate)
        except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError):
            LOG.write(f"{time.strftime('%H:%M:%S')} client closed {name} at {sent}\n")


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    with open(PORT_FILE, "w") as f:
        f.write(str(server.server_address[1]))
    server.serve_forever()


if __name__ == "__main__":
    main()
