#!/usr/bin/env python3
import sys, json, time
sys.path.insert(0, '/home/p/Desktop/dev/poker/pokervariants/test')
from test2 import Table, WS, new_table


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

    h1 = WS("/ws/%s?k=%s" % (tid, hk))
    h1.send({"t": "join", "name": "Host", "stack": 5000})
    w1 = h1.recv()
    assert w1["t"] == "welcome" and w1["host"]
    pid = w1["id"]
    wait_state(h1, lambda s: s["seatedCount"] == 1)
    print("host seated, player id", pid)

    g1 = WS("/ws/%s" % tid)
    g1.send({"t": "join", "name": "G0", "stack": 3000})
    g1.recv()
    wait_state(h1, lambda s: len(s["pending"]) == 1)
    print("guest pending ok")

    h1.s.close()
    time.sleep(0.5)
    print("host 'refreshed' (old socket closed)")

    h2 = WS("/ws/%s?k=%s" % (tid, hk))
    h2.send({"t": "join", "name": "Host", "stack": 5000})
    w2 = h2.recv()
    assert w2["t"] == "welcome", w2
    assert w2["id"] == pid, "reattach should reuse player id %d, got %d" % (pid, w2["id"])
    assert w2["host"], "reattached conn must be host"
    st = wait_state(h2, lambda s: s["seatedCount"] == 1 and len(s["pending"]) == 1)
    assert st["seatedCount"] == 1, "host must still occupy exactly one seat, got %d" % st["seatedCount"]
    hosts = [p for p in st["players"] if p["host"]]
    assert len(hosts) == 1 and hosts[0]["seated"]
    print("host reattached to same player, still one seat")

    h2.send({"t": "approve", "id": st["pending"][0]["id"]})
    st = wait_state(h2, lambda s: s["seatedCount"] == 2)
    assert st["seatedCount"] == 2
    print("guest approved, 2 players seated, no duplicate host")

    host_by_id = [p for p in st["players"] if p["id"] == pid][0]
    assert host_by_id["name"] == "Host" and host_by_id["seat"] >= 0

    h2.send({"t": "set", "paused": True})
    time.sleep(0.3)
    print("HOST REATTACH TEST PASSED")


if __name__ == "__main__":
    main()
