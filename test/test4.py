#!/usr/bin/env python3
import sys, time
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
    host.send({"t": "join", "name": "Host", "stack": 5000})
    w1 = host.recv()
    assert w1["t"] == "welcome" and w1["host"]
    pid, token = w1["id"], w1["token"]
    wait_state(host, lambda s: s["seatedCount"] == 1)
    print("host seated, player id", pid)

    guest = WS("/ws/%s" % tid)
    guest.send({"t": "join", "name": "G0", "stack": 3000})
    guest.recv()
    wait_state(host, lambda s: len(s["pending"]) == 1)
    print("guest pending ok")

    host.s.close()
    st = wait_state(guest, lambda s: s.get("me", {}).get("host") and len([p for p in s["players"] if p["host"]]) == 1)
    hosts = [p for p in st["players"] if p["host"]]
    assert len(hosts) == 1 and hosts[0]["id"] == 2, "online guest should become host"
    assert st["me"]["host"] and st["me"].get("hostKey")
    print("host transferred to online guest")

    h2 = WS("/ws/%s" % tid)
    h2.send({"t": "join", "name": "Host", "stack": 5000, "tok": token})
    w2 = h2.recv()
    assert w2["t"] == "welcome", w2
    assert w2["id"] == pid, "token should reattach the old host player"
    assert not w2["host"], "transferred host must not regain host powers"
    st = wait_state(h2, lambda s: s.get("me", {}).get("id") == pid)
    assert len([p for p in st["players"] if p["host"]]) == 1
    print("old host reattached by token without host powers")

    info = new_table()
    tid2, hk2 = info["id"], info["hostKey"]
    h = WS("/ws/%s?k=%s" % (tid2, hk2))
    h.send({"t": "join", "name": "Solo", "stack": 2000})
    w = h.recv()
    solo_id = w["id"]
    wait_state(h, lambda s: s["seatedCount"] == 1)
    h.s.close()
    time.sleep(0.5)
    h2 = WS("/ws/%s?k=%s" % (tid2, hk2))
    h2.send({"t": "join", "name": "Solo", "stack": 2000})
    w = h2.recv()
    assert w["t"] == "welcome" and w["id"] == solo_id and w["host"]
    print("host key reattaches when no other user is online")
    print("HOST RECONNECT TESTS PASSED")


if __name__ == "__main__":
    main()
