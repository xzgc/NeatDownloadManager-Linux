import sys, threading, http.server, base64, hashlib, os, time
ND = "/tmp/ndmtest"
DATA = open(ND + "/testfile.bin", "rb").read()
SMALL = DATA[:5*1024*1024]
REALM = "NeatTest"
def htdigest(user, realm, pw):
    return hashlib.md5(f"{user}:{realm}:{pw}".encode()).hexdigest()
DIG = htdigest("alice", REALM, "secret123")

def serve(port, handler):
    http.server.ThreadingHTTPServer(("127.0.0.1", port), handler).serve_forever()

def loghdrs(self):
    with open(ND + "/hdrs.log", "a") as f:
        f.write("=== %s\n" % self.path)
        for k, v in self.headers.items(): f.write("%s: %s\n" % (k, v))

class Main(http.server.BaseHTTPRequestHandler):
    def _body(self, start):
        loghdrs(self)
        rng = self.headers.get("Range")
        if rng and rng.startswith("bytes="):
            start = int(rng[6:].split("-")[0])
        code = 206 if (rng and rng.startswith("bytes=")) else 200
        self.send_response(code)
        if code == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, len(DATA)-1, len(DATA)))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(DATA)-start))
        self.end_headers()
        self.wfile.write(DATA[start:])
    def do_GET(self):
        if self.path == "/redir":
            self.send_response(302); self.send_header("Location", "/file.bin"); self.end_headers()
        elif self.path == "/file.bin": self._body(0)
        elif self.path == "/chunked":
            self.send_response(200); self.send_header("Transfer-Encoding", "chunked"); self.end_headers()
            for i in range(0, len(DATA), 65536):
                c = DATA[i:i+65536]
                self.wfile.write(b"%x\r\n" % len(c)); self.wfile.write(c); self.wfile.write(b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/auth-basic" or self.path == "/auth-digest":
            mode = "basic" if self.path.endswith("basic") else "digest"
            hdr = self.headers.get("Authorization", "")
            if mode == "basic":
                ok = hdr == "Basic " + base64.b64encode(b"alice:secret123").decode()
                if not ok:
                    self.send_response(401)
                    self.send_header("WWW-Authenticate", 'Basic realm="%s"' % REALM)
                    self.send_header("Content-Length", "0"); self.end_headers(); return
            else:
                if not hdr.startswith("Digest "):
                    self.send_response(401)
                    self.send_header("WWW-Authenticate",
                        'Digest realm="%s", qop="auth", nonce="abc123nonce", opaque="op"' % REALM)
                    self.send_header("Content-Length", "0"); self.end_headers(); return
                parts = dict(p.strip().split("=", 1) for p in hdr[7:].split(", ") if "=" in p)
                def gv(k): return parts.get(k, "").strip('"')
                ha1 = DIG
                ha2 = hashlib.md5(("GET:" + self.path).encode()).hexdigest()
                resp = hashlib.md5(f"{ha1}:abc123nonce:{gv('nc')}:{gv('cnonce')}:auth:{ha2}".encode()).hexdigest()
                if resp != gv("response"):
                    self.send_response(401)
                    self.send_header("WWW-Authenticate",
                        'Digest realm="%s", qop="auth", nonce="abc123nonce", opaque="op"' % REALM)
                    self.send_header("Content-Length", "0"); self.end_headers(); return
            self._body(0)  # authorized: serve /file.bin content
        elif self.path == "/hls/playlist.m3u8":
            body = ("#EXTM3U\n#EXT-X-TARGETDURATION:2\n" +
                    "".join("#EXTINF:2.0,\nseg%d.ts\n" % i for i in range(24)) +
                    "#EXT-X-ENDLIST\n").encode()
            self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers()
            self.wfile.write(body)
        elif self.path.startswith("/hls/seg"):
            i = int(self.path.split("seg")[1].split(".")[0])
            seg = DATA[i*1310720:(i+1)*1310720]
            self.send_response(200); self.send_header("Content-Length", str(len(seg))); self.end_headers()
            self.wfile.write(seg)
        elif self.path == "/hls/master.m3u8":
            body = ("#EXTM3U\n"
                    "#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=426x240\nlow.m3u8\n"
                    "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720\nplaylist.m3u8\n").encode()
            self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers()
            self.wfile.write(body)
        elif self.path == "/hls/low.m3u8":
            body = ("#EXTM3U\n#EXTINF:1.0,\nseg0.ts\n#EXT-X-ENDLIST\n").encode()
            self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers()
            self.wfile.write(body)
        elif self.path == "/page.html":
            b = b'<script>setTimeout(function(){location.href="/file.bin";},700)</script>'
            self.send_response(200); self.send_header("Content-Type","text/html")
            self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
        else:
            self.send_error(404)
    def do_POST(self):
        if self.path != "/file.bin":
            self.send_error(404); return
        ln = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(ln) if ln else b""
        with open(ND + "/post.log", "a") as f: f.write(body.decode("latin1"))
        self._body(0)
    def log_message(self, *a): pass

class Small(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        rng = self.headers.get("Range")
        start = int(rng[6:].split("-")[0]) if rng and rng.startswith("bytes=") else 0
        code = 206 if rng else 200
        self.send_response(code)
        if rng: self.send_header("Content-Range", "bytes %d-%d/%d" % (start, len(SMALL)-1, len(SMALL)))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(SMALL)-start)); self.end_headers()
        self.wfile.write(SMALL[start:])
    def log_message(self, *a): pass

threading.Thread(target=serve, args=(8766, Main), daemon=True).start()
threading.Thread(target=serve, args=(8767, Small), daemon=True).start()
while True: time.sleep(3600)
