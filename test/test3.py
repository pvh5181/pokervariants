#!/usr/bin/env python3
import sys, time, json
from collections import Counter
from itertools import combinations

sys.path.insert(0, '/home/p/Desktop/dev/poker/pokervariants/test')
from test2 import Table, WS


def card_parse(s):
    r = s[0]
    rank = {'T': 10, 'J': 11, 'Q': 12, 'K': 13, 'A': 14}[r] if r in 'TJQKA' else int(r)
    return (rank, s[1])


def straight_high(uniq, sd):
    for i in range(len(uniq) - 1, -1, -1):
        hi = uniq[i]
        if all(hi - k in uniq for k in range(5)):
            return hi
    if sd:
        if set([14, 6, 7, 8, 9]) <= set(uniq):
            return 9
    else:
        if set([14, 2, 3, 4, 5]) <= set(uniq):
            return 5
    return 0


def eval5(cs, sd=False):
    rs = sorted((c[0] for c in cs), reverse=True)
    suits = [c[1] for c in cs]
    flush = len(set(suits)) == 1
    cnt = Counter(rs)
    uniq = sorted(set(rs), reverse=True)
    sh = straight_high(uniq, sd)
    quads = [r for r, n in cnt.items() if n == 4]
    trips = sorted([r for r, n in cnt.items() if n == 3], reverse=True)
    pairs = sorted([r for r, n in cnt.items() if n == 2], reverse=True)
    singles = sorted([r for r, n in cnt.items() if n == 1], reverse=True)
    if flush and sh:
        return (8, sh, 0, 0, 0, 0)
    if quads:
        return (7, quads[0], singles[0], 0, 0, 0)
    fh_cat = 5 if sd else 6
    if len(trips) == 2:
        return (fh_cat, trips[0], trips[1], 0, 0, 0)
    if trips and pairs:
        return (fh_cat, trips[0], pairs[0], 0, 0, 0)
    flush_cat = 6 if sd else 5
    if flush:
        return (flush_cat, *rs)
    if sh:
        return (4, sh, 0, 0, 0, 0)
    if trips:
        return (3, trips[0], singles[0], singles[1], 0, 0)
    if len(pairs) >= 2:
        return (2, pairs[0], pairs[1], singles[0], 0, 0)
    if pairs:
        return (1, pairs[0], singles[0], singles[1], singles[2], 0)
    return (0, rs[0], rs[1], rs[2], rs[3], rs[4])


def best_holdem(hole, board, use_both, sd):
    if use_both:
        best = None
        for c3 in combinations(board, 3):
            k = eval5(list(hole) + list(c3), sd)
            best = k if best is None or k > best else best
        return best
    best = None
    for c5 in combinations(hole + board, 5):
        k = eval5(list(c5), sd)
        best = k if best is None or k > best else best
    return best


def best_omaha(hole, board, sd):
    best = None
    for h2 in combinations(hole, 2):
        for b3 in combinations(board, 3):
            k = eval5(list(h2) + list(b3), sd)
            best = k if best is None or k > best else best
    return best


def low_of5(cs):
    rs = [1 if r == 14 else r for r, s in cs]
    if any(r > 8 for r in rs) or len(set(rs)) < 5:
        return None
    return tuple(sorted(rs, reverse=True))


def best_low_omaha(hole, board):
    best = None
    for h2 in combinations(hole, 2):
        for b3 in combinations(board, 3):
            k = low_of5(list(h2) + list(b3))
            if k is not None and (best is None or k < best):
                best = k
    return best


def scenario(variant_kw, n_hands=5, check_low=False):
    t = Table(2)
    t.set(**variant_kw)
    for hnum in range(n_hands):
        h = t.play_hand({})
        sd = bool(variant_kw.get("shortdeck"))
        board = [card_parse(c) for c in h["board"]]
        if len(board) < 4:
            continue
        shown = [p for p in h["players"] if not p["folded"] and p.get("hole")]
        if len(shown) < 2:
            continue
        best = {}
        low = {}
        for p in shown:
            hole = [card_parse(c) for c in p["hole"]]
            if variant_kw.get("game") == 1:
                best[p["name"]] = best_omaha(hole, board, sd)
                if check_low:
                    low[p["name"]] = best_low_omaha(hole, board)
            else:
                best[p["name"]] = best_holdem(hole, board, bool(variant_kw.get("greek")), sd)
        winners = set()
        maxk = max(best.values())
        winners |= {n for n, k in best.items() if k == maxk}
        if check_low:
            lows = {n: k for n, k in low.items() if k is not None}
            if lows:
                mink = min(lows.values())
                winners |= {n for n, k in lows.items() if k == mink}
        server_winners = {p["name"] for p in h["players"] if p["won"] > 0}
        if h.get("run2"):
            continue
        assert server_winners == winners, \
            "hand %d: server winners %s != python winners %s\nvariant=%s\nboard=%s\nbest=%s\nplayers=%s" % (
                h["id"], sorted(server_winners), sorted(winners), variant_kw, h["board"], best,
                [(p["name"], p["hole"], p["net"]) for p in h["players"]])
    print("ok %s x%d" % (variant_kw, n_hands))


def main():
    scenario({"sb": 5, "bb": 10}, 6)
    scenario({"sb": 5, "bb": 10, "greek": True}, 6)
    scenario({"sb": 5, "bb": 10, "game": 1, "cards": 4}, 6)
    scenario({"sb": 5, "bb": 10, "game": 1, "cards": 5, "hilo": True}, 6, check_low=True)
    scenario({"sb": 5, "bb": 10, "game": 1, "cards": 6, "hilo": True}, 4, check_low=True)
    scenario({"sb": 5, "bb": 10, "shortdeck": True}, 6)
    scenario({"sb": 5, "bb": 10, "game": 1, "cards": 4, "shortdeck": True}, 6)
    scenario({"sb": 5, "bb": 10, "noRiver": True}, 5)
    scenario({"sb": 5, "bb": 10, "extraRiver": True}, 5)
    print("ALL CROSS-CHECKS PASSED")


if __name__ == "__main__":
    main()
