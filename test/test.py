#!/usr/bin/env python3
import socket, base64, os, json, sys, time, urllib.request

HOST, PORT = '127.0.0.1', 8080

class WS:
    def __init__(self, path):
        self.s = socket.create_connection((HOST, PORT), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, HOST, key))
        self.s.sendall(req.encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        assert b"101" in buf.split(b"\r\n")[0], buf
        self.s.settimeout(8)
        self.buf = b""

    def send(self, obj):
        payload = json.dumps(obj).encode()
        mask = os.urandom(4)
        n = len(payload)
        hdr = b"\x81"
        if n < 126:
            hdr += bytes([n | 0x80])
        elif n < 65536:
            hdr += bytes([126 | 0x80]) + n.to_bytes(2, 'big')
        else:
            hdr += bytes([127 | 0x80]) + n.to_bytes(8, 'big')
        hdr += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.s.sendall(hdr + masked)

    def _parse(self):
        b = self.buf
        if len(b) < 2:
            return None
        op = b[0] & 0xF
        n = b[1] & 0x7F
        off = 2
        if n == 126:
            if len(b) < 4:
                return None
            n = int.from_bytes(b[2:4], 'big')
            off = 4
        elif n == 127:
            if len(b) < 10:
                return None
            n = int.from_bytes(b[2:10], 'big')
            off = 10
        masked = bool(b[1] & 0x80)
        mask = None
        if masked:
            if len(b) < off + 4:
                return None
            mask = b[off:off + 4]
            off += 4
        if len(b) < off + n:
            return None
        payload = b[off:off + n]
        if mask:
            payload = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
        self.buf = b[off + n:]
        if op == 8:
            raise ConnectionError("closed")
        if op == 9:
            self.s.sendall(b"\x8a\x00")
            return self._parse()
        if op in (1, 0):
            return json.loads(payload)
        return self._parse()

    def recv(self, timeout=8):
        end = time.time() + timeout
        self.s.settimeout(timeout)
        while True:
            f = self._parse()
            if f is not None:
                return f
            if time.time() > end:
                raise TimeoutError("no frame")
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                raise TimeoutError("no frame")
            if not d:
                raise ConnectionError("closed")
            self.buf += d

    def recv_t(self, t, timeout=8):
        end = time.time() + timeout
        while time.time() < end:
            m = self.recv(timeout=max(0.1, end - time.time()))
            if m.get("t") == t:
                return m
        raise TimeoutError("no message of type " + t)

    def wait_state(self, pred, timeout=8):
        end = time.time() + timeout
        last = None
        while time.time() < end:
            m = self.recv(timeout=max(0.1, end - time.time()))
            if m.get("t") == "state":
                last = m
                if pred(m):
                    return m
        return last


def main():
    req = urllib.request.urlopen("http://%s:%d/api/new" % (HOST, PORT))
    info = json.loads(req.read())
    tid, hk = info["id"], info["hostKey"]
    print("table", tid, "hostkey", hk[:6] + "...")

    host = WS("/ws/%s?k=%s" % (tid, hk))
    host.send({"t": "join", "name": "Alice", "stack": 2000})
    host.recv_t("welcome")
    st = host.recv_t("state")
    assert st["me"]["seated"], "host should be seated immediately"
    assert st["me"]["host"]
    print("host seated ok")

    g1 = WS("/ws/%s" % tid)
    g1.send({"t": "join", "name": "Bob", "stack": 1500})
    g1.recv_t("welcome")
    g1.recv_t("state")
    g2 = WS("/ws/%s" % tid)
    g2.send({"t": "join", "name": "Carol", "stack": 1500})
    g2.recv_t("welcome")
    g2.recv_t("state")

    st = host.wait_state(lambda m: len(m["pending"]) == 2)
    assert len(st["pending"]) == 2, "expected 2 pending requests, got %d" % len(st["pending"])
    for r in st["pending"]:
        host.send({"t": "approve", "id": r["id"]})
    print("both guests approved")
    host.send({"t": "set", "paused": False})
    host.recv_t("state")

    st = host.wait_state(lambda m: m["hand"]["id"] > 0)
    print("hand", st["hand"]["id"], "started, street", st["hand"]["street"])

    bot_acts = 0
    end = time.time() + 60
    got_hand = False
    last_state = None
    while time.time() < end and not got_hand:
        for name, ws in [("host", host), ("g1", g1), ("g2", g2)]:
            try:
                m = ws.recv(timeout=0.2)
            except TimeoutError:
                continue
            if m.get("t") == "hand":
                got_hand = True
                print("hand completed:", json.dumps(m)[:300])
                break
            if m.get("t") == "state":
                if name == "host":
                    last_state = m
                me = m.get("me") or {}
                if me.get("myTurn") and m["hand"]["phase"] == "betting":
                    if me.get("canCheck") and me.get("toCall", 0) == 0:
                        ws.send({"t": "act", "a": "check"})
                    else:
                        ws.send({"t": "act", "a": "call"})
                    bot_acts += 1
                if me.get("discard"):
                    n = me["discard"]
                    ws.send({"t": "discard", "idx": list(range(n))})
                    bot_acts += 1
    assert got_hand, "hand never completed"
    print("bot actions performed:", bot_acts)
    last_state = host.recv_t("state")

    host.send({"t": "hist"})
    hist = host.recv_t("history")
    print("history entries:", len(hist["hands"]), [h["id"] for h in hist["hands"]])

    for ws in (host, g1, g2):
        while True:
            try:
                ws.recv(timeout=0.15)
            except TimeoutError:
                break
    host.send({"t": "chat", "m": "hello table"})
    cm = g1.recv_t("chat")
    assert cm["m"] == "hello table", cm
    print("chat ok")

    ledger = last_state["ledger"]
    assert len(ledger) == 3, "ledger should have 3 rows, got %d" % len(ledger)
    for row in ledger:
        assert row["stack"] == row["bought"] + row["wl"], "ledger math broken: %s" % row
    print("ledger ok:", [(r["name"], r["wl"]) for r in ledger])

    host.send({"t": "set", "game": 1, "cards": 4, "bet": 1, "sb": 2, "bb": 4})
    st = host.recv_t("state")
    assert st["settings"]["game"] == 1 and st["settings"]["cards"] == 4
    print("variant switched to Omaha PL 4-card ok, maxPlayers =", st["settings"]["maxPlayers"])
    host.send({"t": "set", "cards": 6})
    st = host.recv_t("state")
    print("6-card maxPlayers =", st["settings"]["maxPlayers"])

    print("ALL TESTS PASSED")


if __name__ == "__main__":
    main()
