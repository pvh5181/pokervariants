#!/usr/bin/env python3
import sys, json, time
sys.path.insert(0, '/home/p/Desktop/dev/poker/pokervariants/test')
from test2 import WS, new_table


def wait_state(ws, pred, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        m = ws.recv(timeout=max(0.1, end - time.time()))
        if m.get("t") == "state" and pred(m):
            return m
    raise TimeoutError("state not seen")


def main():
    info = new_table()
    tid, hk = info["id"], info["hostKey"]

    host = WS("/ws/%s?k=%s" % (tid, hk))
    host.send({"t": "join", "name": "H", "stack": 5000})
    host.recv()
    wait_state(host, lambda s: s["seatedCount"] == 1)
    host.send({"t": "set", "paused": True})
    host.recv()

    w = WS("/ws/%s" % tid)
    w.send({"t": "watch", "name": "Watcher"})
    wl = w.recv()
    assert wl["t"] == "welcome"
    st = wait_state(w, lambda s: s.get("me") and s["me"]["name"] == "Watcher")
    assert not st["me"]["seated"] and not st["me"]["pending"], st["me"]
    assert st["seatedCount"] == 1
    print("watch mode ok: spectator sees table without sitting")

    w.send({"t": "chat", "m": "hello from the rail"})
    cm = host.recv()
    found = False
    end = time.time() + 5
    while time.time() < end:
        if cm.get("t") == "chat" and cm.get("m") == "hello from the rail":
            found = True
            break
        cm = host.recv()
    assert found
    print("spectator chat ok")

    w.send({"t": "join", "name": "Watcher", "stack": 2000, "seat": 7})
    st = wait_state(host, lambda s: len(s["pending"]) == 1)
    host.send({"t": "approve", "id": st["pending"][0]["id"]})
    st = wait_state(w, lambda s: s["me"]["seated"])
    assert st["me"]["seat"] == 7, "requested seat 7, got %s" % st["me"]["seat"]
    print("click-to-sit ok: seated at requested seat 7")

    host.send({"t": "set", "paused": False})
    st = wait_state(w, lambda s: s["hand"]["id"] > 0)
    inhand = [p for p in st["players"] if p["state"] != "waiting"]
    assert all(p.get("n") == 2 for p in inhand), "hold'em players should show n=2 card backs: %s" % [(p["name"], p.get("n")) for p in inhand]
    assert st["turns"][0]["window"] > 0, "turn should carry a window"
    print("card-back count (n=2) and timer window ok")

    def play_out():
        done = False
        end = time.time() + 30
        while time.time() < end and not done:
            for ws in (host, w):
                try:
                    m = ws.recv(timeout=0.2)
                except TimeoutError:
                    continue
                if m.get("t") == "hand":
                    done = True
                    break
                if m.get("t") == "state":
                    me = m.get("me") or {}
                    if me.get("myTurn") and m["hand"]["phase"] == "betting":
                        if me.get("canCheck") and me.get("toCall", 0) == 0:
                            ws.send({"t": "act", "a": "check"})
                        else:
                            ws.send({"t": "act", "a": "call"})
        assert done, "hand did not finish"

    host.send({"t": "set", "paused": True})
    host.recv()
    play_out()
    host.send({"t": "set", "game": 1, "cards": 4})
    host.recv()
    host.send({"t": "set", "paused": False})
    st = wait_state(w, lambda s: s["hand"]["id"] > 0 and s["hand"]["phase"] == "betting")
    assert st["settings"]["game"] == 1
    inhand = [p for p in st["players"] if p["state"] != "waiting"]
    assert all(p.get("n") == 4 for p in inhand), "omaha 4c should show n=4 card backs: %s" % [(p["name"], p.get("n")) for p in inhand]
    print("card-back count (n=4) ok")
    print("ALL NEW FEATURE TESTS PASSED")


if __name__ == "__main__":
    main()
