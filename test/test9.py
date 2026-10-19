#!/usr/bin/env python3
import sys, json, time, urllib.request
sys.path.insert(0, '/home/p/Desktop/dev/poker/pokervariants/test')
from test2 import Table


def wait_host(ws, timeout=5):
    end = time.time() + timeout
    while time.time() < end:
        m = ws.recv(timeout=max(0.1, end - time.time()))
        if m.get("t") == "state":
            hosts = [p for p in m["players"] if p["host"]]
            if len(hosts) == 1:
                return m


def main():
    print("== host disconnect elects online player ==")
    t = Table(1)
    old_host = t.clients[0]
    guest = t.clients[1]
    old_host.s.close()
    st = wait_host(guest)
    hosts = [p for p in st["players"] if p["host"]]
    assert len(hosts) == 1, hosts
    assert hosts[0]["id"] == 2, "guest should become host: %s" % hosts
    assert st["me"]["host"], "guest must see itself as host"
    assert st["me"].get("hostKey"), "new host must receive a host key"
    print("ok elected", hosts[0]["name"])

    print("== closed table keeps id and ledger ==")
    guest.s.close()
    time.sleep(65)
    url = "http://127.0.0.1:8080/api/table/%s" % t.tid
    info = json.loads(urllib.request.urlopen(url).read())
    assert info["closed"], info
    assert info["id"] == t.tid
    assert len(info["ledger"]) == 2, info["ledger"]
    print("ok closed table", info["id"], "ledger rows", len(info["ledger"]))
    print("ALL TABLE LIFECYCLE TESTS PASSED")


if __name__ == "__main__":
    main()
