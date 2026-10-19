#!/usr/bin/env python3
import sys, time
sys.path.insert(0, '/home/p/Desktop/dev/poker/pokervariants/test')
from test2 import Table


def capture_states(t, timeout=3):
    states = {}
    end = time.time() + timeout
    while len(states) < len(t.clients) and time.time() < end:
        for i, c in enumerate(t.clients):
            try:
                m = c.recv(timeout=0.1)
            except TimeoutError:
                continue
            if m.get("t") == "state":
                states[i] = m
    assert len(states) == len(t.clients), "missing states: %d/%d" % (len(states), len(t.clients))
    return states


def scenario_holdem_cards():
    print("== holdem multi-card variants ==")
    t = Table(2)
    t.set(game=0, cards=3, pineapple=True, sb=5, bb=10)
    h = t.play_hand({})
    for p in h["players"]:
        if not p["folded"]:
            assert len(p["hole"]) == 2, "holdem pineapple showdown should have 2 hole cards"
    print("ok holdem pineapple")

    t = Table(2)
    t.set(game=0, cards=4, irish=True, sb=5, bb=10)
    h = t.play_hand({})
    for p in h["players"]:
        if not p["folded"]:
            assert len(p["hole"]) == 2, "holdem irish showdown should have 2 hole cards"
    print("ok holdem irish")

    t = Table(2)
    t.set(game=0, cards=5, sb=5, bb=10)
    h = t.play_hand({})
    shown = [p for p in h["players"] if p.get("hole")]
    assert shown and all(len(p["hole"]) == 5 for p in shown), "5-card holdem should keep 5 hole cards"
    print("ok holdem 5-card")


def scenario_greek_cards_guard():
    print("== greek only with two cards ==")
    t = Table(1)
    st = t.set(game=0, cards=3, greek=True)
    assert st["settings"]["cards"] == 3
    assert not st["settings"]["greek"], "greek must be disabled for multi-card holdem"
    print("ok")


def scenario_host_transfer():
    print("== host transfer ==")
    t = Table(1)
    t.host().send({"t": "transfer", "id": 2})
    states = capture_states(t)
    hosts = [p for p in states[0]["players"] if p["host"]]
    assert len(hosts) == 1 and hosts[0]["id"] == 2, hosts
    assert not states[0]["me"]["host"], "old host should lose host"
    assert states[1]["me"]["host"], "new host should gain host"
    assert states[1]["me"].get("hostKey"), "new host should receive a host key"
    print("ok only one host, key delivered")


def scenario_negative_chips():
    print("== host removes chips ==")
    t = Table(1)
    t.drain()
    t.host().send({"t": "chips", "id": 2, "amt": -500})
    states = capture_states(t)
    guest = [p for p in states[0]["players"] if p["id"] == 2][0]
    assert guest["stack"] == 4500, guest
    row = [r for r in states[0]["ledger"] if r["id"] == 2][0]
    assert row["stack"] == 4500 and row["bought"] == 4500, row
    print("ok removed 5.00, ledger adjusted")


def main():
    scenario_holdem_cards()
    scenario_greek_cards_guard()
    scenario_host_transfer()
    scenario_negative_chips()
    print("ALL NEW FEATURE TESTS PASSED")


if __name__ == "__main__":
    main()
