#!/usr/bin/env python3
import sys, json, time
sys.path.insert(0, '/home/p/Desktop/dev/poker/pokervariants/test')
from test2 import Table, WS


def scenario(n_guests, actions, label, run2=False):
    t = Table(n_guests)
    t.set(paused=False, runTwice=run2)
    last_hand = None
    over_state = None
    got_hand = False
    end = time.time() + 90
    while time.time() < end and not (got_hand and over_state):
        for i, c in enumerate(t.clients):
            try:
                m = c.recv(timeout=0.2)
            except TimeoutError:
                continue
            if m.get("t") == "hand":
                got_hand = True
                last_hand = m
            if m.get("t") == "state":
                if m["hand"]["phase"] == "over":
                    over_state = m
                me = m.get("me") or {}
                if me.get("run2Asked") and not me.get("run2Answered"):
                    c.send({"t": "run2", "ans": True})
                if me.get("myTurn") and m["hand"]["phase"] == "betting":
                    act = actions.get(i, [])
                    if act:
                        a = act.pop(0)
                        if isinstance(a, int):
                            c.send({"t": "act", "a": "bet", "amt": a})
                        elif a == "allin":
                            c.send({"t": "act", "a": "allin"})
                        elif a == "call":
                            c.send({"t": "act", "a": "call"})
                        elif a == "fold":
                            c.send({"t": "act", "a": "fold"})
                        else:
                            c.send({"t": "act", "a": "check"})
                    else:
                        c.send({"t": "act", "a": "check"})
    assert got_hand, "%s: hand never completed" % label
    assert over_state is not None, "%s: no over state" % label
    board = over_state["hand"]["board"]
    assert len(board) == 5, "%s: board should be 5 cards at end, got %d: %s" % (label, len(board), board)
    nets = [p["net"] for p in over_state["last"]["players"]]
    assert sum(nets) == 0, "%s: chips not conserved: %s" % (label, nets)
    return over_state


def main():
    print("== 3 players all-in preflop ==")
    scenario(2, {0: ["allin"], 1: ["allin"], 2: ["allin"]}, "3-way allin")
    print("ok")

    print("== HU all-in, caller has chips left ==")
    scenario(1, {0: ["allin"], 1: ["call"]}, "caller with chips")
    print("ok")

    print("== all-in on flop ==")
    scenario(1, {0: ["check", "allin"], 1: ["check", "allin"]}, "flop allin")
    print("ok")

    print("== all-in preflop with run twice ==")
    st = scenario(1, {0: ["allin"], 1: ["allin"]}, "run2 allin", run2=True)
    assert st["last"]["run2"], "run2 should have happened"
    print("ok")

    print("== all-in after one folds ==")
    scenario(2, {0: ["fold"], 1: ["allin"], 2: ["call"]}, "fold then allin")
    print("ok")

    print("ALL ALL-IN SCENARIOS PASSED")


if __name__ == "__main__":
    main()
