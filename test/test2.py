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
        self.s.settimeout(10)
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

    def recv(self, timeout=10):
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


def new_table():
    req = urllib.request.urlopen("http://%s:%d/api/new" % (HOST, PORT))
    return json.loads(req.read())


class Table:
    def __init__(self, n_guests=2, stacks=None):
        info = new_table()
        self.tid, self.hk = info["id"], info["hostKey"]
        stacks = stacks or [5000] * (n_guests + 1)
        self.clients = []
        host = WS("/ws/%s?k=%s" % (self.tid, self.hk))
        host.send({"t": "join", "name": "H", "stack": stacks[0]})
        host.recv()
        host.recv()
        self.clients.append(host)
        for i in range(n_guests):
            g = WS("/ws/%s" % self.tid)
            g.send({"t": "join", "name": "G%d" % i, "stack": stacks[i + 1]})
            g.recv()
            self.clients.append(g)
        self.drain()
        host.send({"t": "set", "paused": True})
        self.drain()
        for i in range(n_guests):
            host.send({"t": "approve", "id": i + 1})
        self.drain()

    def host(self):
        return self.clients[0]

    def get_state(self, idx=0):
        while True:
            m = self.clients[idx].recv()
            if m.get("t") == "state":
                return m
            if m.get("t") == "hand":
                self.last_hand = m

    def drain(self):
        for c in self.clients:
            while True:
                try:
                    c.recv(timeout=0.15)
                except TimeoutError:
                    break

    def set(self, **kw):
        self.host().send(dict({"t": "set"}, **kw))
        return self.get_state()

    def play_hand(self, actions, run2_answer=None, timeout=60):
        self.host().send({"t": "set", "paused": False})
        self.last_hand = None
        pending = {k: list(v) if v is not None else None for k, v in actions.items()}
        done = False
        end = time.time() + timeout
        states = [None] * len(self.clients)
        while time.time() < end and not done:
            for i, c in enumerate(self.clients):
                try:
                    m = c.recv(timeout=0.1)
                except TimeoutError:
                    continue
                if m.get("t") == "hand":
                    self.last_hand = m
                    done = True
                    break
                if m.get("t") == "state":
                    states[i] = m
                    me = m.get("me") or {}
                    if me.get("run2Asked") and not me.get("run2Answered") and run2_answer is not None:
                        c.send({"t": "run2", "ans": run2_answer})
                    if me.get("myTurn") and m["hand"]["phase"] == "betting":
                        if i in pending and pending[i] is None:
                            continue
                        act = pending.get(i, [])
                        if act:
                            a = act.pop(0)
                            if isinstance(a, int):
                                a = {"t": "act", "a": "bet", "amt": a}
                            elif a == "check":
                                a = {"t": "act", "a": "check"}
                            elif a == "call":
                                a = {"t": "act", "a": "call"}
                            elif a == "fold":
                                a = {"t": "act", "a": "fold"}
                            elif a == "allin":
                                a = {"t": "act", "a": "allin"}
                            c.send(a)
                        else:
                            if me.get("canCheck") and me.get("toCall", 0) == 0:
                                c.send({"t": "act", "a": "check"})
                            else:
                                c.send({"t": "act", "a": "call"})
                    if me.get("discard"):
                        n = me["discard"]
                        c.send({"t": "discard", "idx": list(range(n))})
        assert done, "hand did not complete"
        return self.last_hand

    def nets_sum_zero(self):
        h = self.last_hand
        s = sum(p["net"] for p in h["players"])
        assert s == 0, "chips not conserved: nets=%s" % [p["net"] for p in h["players"]]
        return s


def scenario_omaha_hilo_pineapple():
    print("== omaha hilo pineapple ==")
    t = Table(2)
    t.set(game=1, cards=4, hilo=True, pineapple=True, sb=10, bb=20)
    h = t.play_hand({})
    assert len(h["board"]) == 5, "board should be 5 cards"
    for p in h["players"]:
        if not p["folded"]:
            assert len(p["hole"]) == 2, "omaha showdown hole should be 2 cards after pineapple discard"
    t.nets_sum_zero()
    print("ok board=%s" % h["board"])


def scenario_run_twice():
    print("== run it twice ==")
    t = Table(1)
    t.set(runTwice=True, sb=10, bb=20)
    h = t.play_hand({0: ["allin"], 1: ["allin"]}, run2_answer=True)
    assert h.get("run2"), "run2 should be true"
    assert len(h.get("board2", [])) == 5, "board2 should be full"
    t.nets_sum_zero()
    print("ok boards=%s / %s" % (h["board"], h["board2"]))


def scenario_run_twice_decline():
    print("== run it twice (declined) ==")
    t = Table(1)
    t.set(runTwice=True, sb=10, bb=20)
    h = t.play_hand({0: ["allin"], 1: ["allin"]}, run2_answer=False)
    assert not h.get("run2"), "run2 should be false"
    t.nets_sum_zero()
    print("ok")


def scenario_timebank():
    print("== timebank auto-fold ==")
    t = Table(2)
    t.set(sec=5)
    h = t.play_hand({1: None}, timeout=30)
    g0 = [p for p in h["players"] if p["name"] == "G0"]
    assert g0[0]["folded"], "G0 should have timed out and folded"
    t.nets_sum_zero()
    print("ok")


def scenario_no_river():
    print("== no river ==")
    t = Table(2)
    t.set(noRiver=True, sb=5, bb=10)
    h = t.play_hand({})
    assert len(h["board"]) == 4, "no-river board should be 4 cards, got %d" % len(h["board"])
    t.nets_sum_zero()
    print("ok board=%s" % h["board"])


def scenario_extra_river():
    print("== extra river ==")
    t = Table(2)
    t.set(extraRiver=True, sb=5, bb=10)
    h = t.play_hand({})
    assert len(h["board"]) == 6, "extra-river board should be 6 cards, got %d" % len(h["board"])
    t.nets_sum_zero()
    print("ok board=%s" % h["board"])


def scenario_greek():
    print("== greek holdem ==")
    t = Table(2)
    t.set(greek=True, sb=5, bb=10)
    h = t.play_hand({})
    t.nets_sum_zero()
    print("ok board=%s" % h["board"])


def scenario_shortdeck():
    print("== shortdeck omaha ==")
    t = Table(2)
    t.set(shortdeck=True, game=1, cards=4, sb=5, bb=10)
    h = t.play_hand({})
    allowed = set("6789TJQKA")
    for c in h["board"]:
        assert c[0] in allowed, "shortdeck card %s invalid" % c
    t.nets_sum_zero()
    print("ok board=%s" % h["board"])


def scenario_side_pots():
    print("== side pots ==")
    t = Table(2, stacks=[10000, 2000, 10000])
    t.set(sb=10, bb=20)
    h = t.play_hand({0: ["call", "call", "check", "check"],
                     1: ["allin"],
                     2: ["call", 3000, "check", "check"]})
    nets = {p["name"]: p["net"] for p in h["players"]}
    assert sum(nets.values()) == 0, "chips not conserved: %s" % nets
    assert nets["G0"] in (-2000, 0, 1000, 4000), "short all-in net unexpected: %s" % nets
    assert nets["H"] + nets["G1"] == -nets["G0"]
    print("ok nets=%s" % nets)


def main():
    scenario_omaha_hilo_pineapple()
    scenario_run_twice()
    scenario_run_twice_decline()
    scenario_timebank()
    scenario_no_river()
    scenario_extra_river()
    scenario_greek()
    scenario_shortdeck()
    scenario_side_pots()
    print("ALL SCENARIOS PASSED")


if __name__ == "__main__":
    main()
