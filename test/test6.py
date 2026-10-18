#!/usr/bin/env python3
import sys, json, time
sys.path.insert(0, '/home/p/Desktop/dev/poker/pokervariants/test')
from test2 import Table, WS


def scenario_pause_and_winner():
    print("== 2s pause + winner info ==")
    t = Table(2)
    t.set(paused=False)
    t_hand1 = None
    t_hand2 = None
    over_state = None
    myturn_seen = False
    end = time.time() + 120
    while time.time() < end:
        for i, c in enumerate(t.clients):
            try:
                m = c.recv(timeout=0.2)
            except TimeoutError:
                continue
            if m.get("t") == "hand":
                if t_hand1 is None:
                    t_hand1 = time.time()
            if m.get("t") == "state":
                if m["hand"]["phase"] == "over" and over_state is None:
                    over_state = m
                if m["hand"]["id"] > 0 and m["hand"]["phase"] == "betting":
                    if t_hand1 is not None and t_hand2 is None and m["hand"]["id"] > 1:
                        t_hand2 = time.time()
                me = m.get("me") or {}
                if me.get("myTurn"):
                    assert "halfPot" in me and "threePot" in me, "missing quick-bet values"
                    myturn_seen = True
                    if me.get("canCheck") and me.get("toCall", 0) == 0:
                        c.send({"t": "act", "a": "check"})
                    else:
                        c.send({"t": "act", "a": "call"})
        if t_hand2 is not None:
            break
    assert t_hand1 is not None and t_hand2 is not None, "need two hands"
    gap = t_hand2 - t_hand1
    assert gap >= 1.8, "next hand started too soon: %.2fs" % gap
    print("ok pause between hands: %.2fs" % gap)
    assert myturn_seen, "no turn seen"
    assert over_state is not None, "no 'over' state seen"
    assert over_state.get("last"), "state should carry last hand summary"
    winners = [p for p in over_state["last"]["players"] if p["won"] > 0]
    assert winners, "summary should have a winner with won>0"
    print("ok winner info in last: %s" % [(w["name"], w["won"]) for w in winners])


def scenario_show_muck():
    print("== show/muck after everyone folds ==")
    t = Table(2)
    t.set(paused=False)
    winner_ws = None
    winner_name = None
    got = {"hand": False, "choice": False}
    end = time.time() + 60
    while time.time() < end:
        for i, c in enumerate(t.clients):
            try:
                m = c.recv(timeout=0.2)
            except TimeoutError:
                continue
            if m.get("t") == "state":
                me = m.get("me") or {}
                if me.get("showChoice"):
                    got["choice"] = True
                    winner_ws = c
                    winner_name = me["name"]
                if me.get("myTurn") and m["hand"]["phase"] == "betting":
                    if me.get("canCheck") and me.get("toCall", 0) == 0:
                        c.send({"t": "act", "a": "fold"})
                    else:
                        c.send({"t": "act", "a": "call"})
            if m.get("t") == "hand":
                got["hand"] = True
        if got["choice"] and got["hand"]:
            break
    assert got["choice"], "winner should get showChoice"
    print("winner with showChoice:", winner_name)
    observer = t.clients[1] if winner_ws is t.clients[0] else t.clients[0]
    winner_ws.send({"t": "show"})
    st = None
    end = time.time() + 10
    while time.time() < end:
        m = observer.recv(timeout=max(0.1, end - time.time()))
        if m.get("t") == "state" and m["hand"]["phase"] == "over":
            w = [p for p in m["players"] if p["name"] == winner_name][0]
            if w.get("hole"):
                st = m
                break
    assert st is not None, "shown hand never became visible"
    w = [p for p in st["players"] if p["name"] == winner_name][0]
    assert w.get("hole"), "shown hand should be visible to others"
    assert w["hole"] and len(w["hole"]) == 2
    lh = [p for p in st["last"]["players"] if p["name"] == winner_name][0]
    assert lh.get("hole"), "history should include the shown hand"
    print("ok show/muck flow, shown hand:", w["hole"])


def scenario_allin_pacing():
    print("== all-in 1s reveals ==")
    t = Table(1)
    t.set(paused=False)
    t0 = None
    streets = {}
    acted = False
    end = time.time() + 60
    while time.time() < end:
        for i, c in enumerate(t.clients):
            try:
                m = c.recv(timeout=0.2)
            except TimeoutError:
                continue
            if m.get("t") == "state":
                h = m["hand"]
                if h["id"] > 0 and h["street"] not in streets:
                    streets[h["street"]] = time.time()
                if h["street"] == 0 and h["phase"] == "betting" and t0 is None:
                    t0 = time.time()
                me = m.get("me") or {}
                if me.get("myTurn") and h["phase"] == "betting" and not acted:
                    acted = True
                    c.send({"t": "act", "a": "allin"})
                elif me.get("myTurn") and h["phase"] == "betting":
                    c.send({"t": "act", "a": "call"})
            if m.get("t") == "hand":
                if streets.get(3) is not None:
                    end = 0
                    break
        if end == 0:
            break
    assert 3 in streets, "river never seen: %s" % streets
    gap1 = streets[1] - streets[0]
    gap2 = streets[2] - streets[1]
    gap3 = streets[3] - streets[2]
    for name, g in [("flop", gap1), ("turn", gap2), ("river", gap3)]:
        assert 0.8 <= g <= 4.5, "%s reveal gap out of range: %.2fs" % (name, g)
    print("ok reveal gaps: flop %.2fs turn %.2fs river %.2fs" % (gap1, gap2, gap3))


def scenario_pot_semantics():
    print("== pot freezes during street, bet indicators ==")
    t = Table(2)
    t.set(paused=False, sb=10, bb=20)
    seen_raise_state = None
    done = False
    end = time.time() + 60
    while time.time() < end and not done:
        for i, c in enumerate(t.clients):
            try:
                m = c.recv(timeout=0.2)
            except TimeoutError:
                continue
            if m.get("t") == "state":
                me = m.get("me") or {}
                if me.get("myTurn") and m["hand"]["phase"] == "betting":
                    if i == 0 and m["hand"]["street"] == 0 and not seen_raise_state:
                        c.send({"t": "act", "a": "bet", "amt": 60})
                    else:
                        if me.get("canCheck") and me.get("toCall", 0) == 0:
                            c.send({"t": "act", "a": "fold"})
                        else:
                            c.send({"t": "act", "a": "fold"})
                if seen_raise_state is None:
                    h = m["hand"]
                    me0 = [p for p in m["players"] if p["id"] == m["me"]["id"]][0]
                    if me0.get("bet") == 60 and h["street"] == 0:
                        seen_raise_state = m
            if m.get("t") == "hand":
                done = True
                break
    assert seen_raise_state is not None, "did not observe the raise state"
    h = seen_raise_state["hand"]
    assert h["pot"] == 30, "pot should show only blinds during street, got %d" % h["pot"]
    raiser = [p for p in seen_raise_state["players"] if p["bet"] == 60]
    assert len(raiser) == 1, "raiser indicator missing"
    sbp = [p for p in seen_raise_state["players"] if p["bet"] == 10]
    bbp = [p for p in seen_raise_state["players"] if p["bet"] == 20]
    assert len(sbp) == 1 and len(bbp) == 1, "blind indicators missing"
    print("ok pot stayed at blinds (30) while bets shown per player")


def main():
    scenario_pause_and_winner()
    scenario_show_muck()
    scenario_allin_pacing()
    scenario_pot_semantics()
    print("ALL NEW FLOW TESTS PASSED")


if __name__ == "__main__":
    main()
