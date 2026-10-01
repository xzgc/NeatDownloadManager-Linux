#!/usr/bin/env python3
"""Fake NeatDM browser extension: byte-level WS client speaking the exact
protocol from bg.js (ws://127.0.0.1:10007/download, subprotocol neatextension.v1,
masked text frames, line-based download messages)."""
import base64, hashlib, os, socket, struct, sys, time

import os
HOST, PORT = '127.0.0.1', int(os.environ.get('NEATDM_WS_PORT', '10007'))

def ws_connect():
    key = base64.b64encode(os.urandom(16)).decode()
    s = socket.create_connection((HOST, PORT), timeout=10)
    s.sendall((f"GET /download HTTP/1.1\r\nHost: {HOST}:{PORT}\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
               "Sec-WebSocket-Protocol: neatextension.v1\r\n\r\n").encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = s.recv(4096)
        if not chunk:
            raise SystemExit(f"handshake failed: {resp!r}")
        resp += chunk
    head = resp.split(b"\r\n\r\n")[0].decode()
    expect = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
    assert "101" in head.split("\r\n")[0], head
    assert f"Sec-WebSocket-Accept: {expect}" in head, head
    assert "neatextension.v1" in head, head
    print("HANDSHAKE OK (101 + accept + subprotocol)")
    return s, resp.split(b"\r\n\r\n", 1)[1]

def send_text(s, text):
    payload = text.encode()
    mask = os.urandom(4)
    hdr = bytearray([0x81])
    n = len(payload)
    if n < 126:
        hdr.append(0x80 | n)
    elif n <= 0xFFFF:
        hdr.append(0x80 | 126); hdr += struct.pack(">H", n)
    else:
        hdr.append(0x80 | 127); hdr += struct.pack(">Q", n)
    hdr += mask
    s.sendall(bytes(hdr) + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

def recv_frames(s, leftover=b"", timeout=5):
    end = time.time() + timeout
    out = []
    buf = leftover
    while time.time() < end:
        try:
            s.settimeout(max(0.1, end - time.time()))
            data = s.recv(4096)
            if data:
                buf += data
        except socket.timeout:
            pass
        while len(buf) >= 2:
            op = buf[0] & 0x0F
            ln = buf[1] & 0x7F
            off = 2
            if ln == 126:
                if len(buf) < 4: break
                ln = struct.unpack(">H", buf[2:4])[0]; off = 4
            elif ln == 127:
                if len(buf) < 10: break
                ln = struct.unpack(">Q", buf[2:10])[0]; off = 10
            if len(buf) < off + ln: break
            payload = buf[off:off+ln]
            buf = buf[off+ln:]
            if op == 1:
                out.append(payload.decode())
        if out and time.time() > end - 3:
            break
    return out, buf

if __name__ == "__main__":
    url = sys.argv[1]
    fname = sys.argv[2] if len(sys.argv) > 2 else "ext-download.bin"
    s, leftover = ws_connect()
    msgs, leftover = recv_frames(s, leftover, timeout=4)
    print("SERVER PUSHED:", msgs)
    assert any(m == "nowaiting" for m in msgs), "expected nowaiting"
    assert any(m.startswith("ShowPanelChrome=") for m in msgs), "expected ShowPanel flags"

    mode = sys.argv[3] if len(sys.argv) > 3 else ""
    method = "POST" if mode == "post" else "GET"
    ltype = "hls" if mode == "hls" else "normal"
    lines = [f"1:{method}", f"2:{url}", "6:" + ltype]
    if method == "POST":
        lines += ["Content-Type: application/x-www-form-urlencoded",
                  "__0NeatPostData9__:user=bob&file=report"]
    lines += [
        f"3:{fname}",
        "4:Fake Extension Test Page",
        "5:http://127.0.0.1:8766/page.html",
        "Cookie: session=abc123; token=xyz",
        "Referer: http://127.0.0.1:8766/page.html",
        "Origin: http://127.0.0.1:8766",
        "x-auth-token: neat-test-42",
    ]
    send_text(s, "\r\n".join(lines) + "\r\n")
    print("DOWNLOAD MESSAGE SENT")
    # keep the socket alive a bit (like the extension does)
    recv_frames(s, leftover, timeout=8)
    s.close()
    print("DONE")
