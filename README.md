# Poker Variants

A self-contained, account-free poker site: a C++17 backend (HTTP + WebSocket, zero external
dependencies) serving a lightweight vanilla-JavaScript frontend. Any player can create a table,
share the link, and others join with their preferred name. The first player to create a table is
the host.

## Build & run

```sh
make            # builds ./pokerserver
./pokerserver   # serves on port 8080 (override: ./pokerserver 9000 or PORT=9000)
```

Open `http://localhost:8080` in a browser.

For a production deployment, see [DEPLOY.md](DEPLOY.md). It covers a Namecheap
VPS with Ubuntu, systemd, an Nginx WebSocket reverse proxy, DNS, firewalling, and
Let's Encrypt TLS. Namecheap shared hosting cannot run this backend.

## How it works

1. **Create table** — enter your name, starting stack and blinds. You become the host and are
   seated immediately. The host key stays in your browser's session storage — it is never
   included in the table link, so anyone you share the link with joins as a regular guest.
2. **Share the link** — the "Copy link" button copies `http://host/#/t/<tableId>`. Guests open
   the link, enter their name and buy-in stack (or choose "Watch only"), and their seat request
   appears in your host panel for approval. Guests can also click an empty seat at the table to
   request it.
3. **Play** — every table starts paused; the host starts the game with the pause/play button
   (⏸/▶) in the top bar. Hands run automatically from then on.
4. **Host hand-off** — the host can transfer host permissions from the host panel. If the host
   leaves their seat or disconnects, a random online player becomes host. Exactly one player is
   host.
5. **Table lifecycle** — if nobody is connected for 60 seconds, the table closes. Its link then
   shows only the table ID and the final ledger. Closed tables are removed from memory after
   24 hours.

There are no accounts and no persistence: tables, ledgers and hand history live in server memory
for the lifetime of the table. Player and host resume tokens are kept in the browser's session
storage so refreshes can reattach without creating duplicate players.

## Host powers

- Set small blind / big blind
- Allow running it twice (players must agree when an all-in situation occurs)
- Pause / resume the game
- Set the decision-time clock that applies to all bettors; every player has a recharging time
  bank (starts at 60s, +15s per hand, capped at 120s). When the main clock expires the bank
  takes over; when the bank is empty the player is folded.
- Approve / reject seat requests (name + proposed stack size) and rebuy requests
- Add chips to a player's balance, or enter a negative amount to remove chips
- Set a player's stack to an exact amount
- Transfer host permissions to another player; only one host exists at a time
- Remove players from the table

## Game variants

Hold'em (with a 2/3/4/5/6-card toggle):
- No Limit / Pot Limit bet sizing
- Pineapple: discard down to two hole cards after preflop betting, before the flop
- Irish: discard down to two hole cards after flop betting, before the turn
- Greek: standard two-card hold'em, but both hole cards must be used at showdown
  (unavailable when more than two hole cards are selected)
- Shortdeck: 2-5 removed from the deck; a flush beats a full house

With more than two Hold'em hole cards and no discard variant, the best five-card hand from all
hole and board cards is used.

Omaha (with a 3/4/5/6-card toggle):
- No Limit / Pot Limit bet sizing
- Pineapple: discard down to two hole cards after preflop betting, before the flop
- Irish: discard down to two hole cards after flop betting, before the turn
- Shortdeck: 2-5 removed, flush beats full house (cannot combine with Hi/Lo)
- Hi/Lo: 8-or-better low splits the pot (cannot combine with Shortdeck)

Board structures ("Other"):
- No river: showdown happens on the turn (4 board cards)
- Extra river: a 6th board card is dealt followed by another betting round

Table size: up to 10 players, limited by the format. Maximum is
`min(10, floor((deckSize - boardCards) / holeCards))`. Examples: 5-card Omaha caps at 9,
6-card Omaha at 7 (8 players x 6 hole cards + 5 board cards = 53 > 52, so 7 is the largest
playable table), 6-card shortdeck at 5.

## Information flow

The server never reveals game content ahead of its betting stage: community cards are only
broadcast after the previous betting round has closed, hole cards are only revealed at
showdown, and spectators see the same information as players.

## Features

- WebSocket connection per table; full state snapshots plus events for chat and hands
- Small chat in the bottom-left corner of the table
- Hand history log (last 500 hands) with boards, hole cards shown at showdown and per-player net
- Table ledger: what each player bought in for, their current stack and won/lost
- Run-it-twice with two independent runouts and per-board pot splits
- Side pots, all-in short raises, no-limit and pot-limit raising rules
- Decision clock with recharging time bank; disconnect auto check/fold
- 2-second pause at the end of every hand with gold winner highlight and "+amount" badges,
  plus a show-or-muck choice when everyone folded to the winner
- All-in runouts revealed one street per second; the winner's board cards are highlighted at
  showdown while unused cards fade
- Bet menu with slider, typed input and Min / ½ Pot / ¾ Pot / All-in quick buttons
- Live hand-rank display (e.g. "Two Pair (A, 5)") in the bottom-right corner
- Two-decimal bet-chip amounts (e.g. `3.75`, not `3.8`)
- Closed-table page showing the table ID and final ledger after everyone goes offline
- Players can move between empty seats when not in a hand

## Layout

```
src/
  server.cpp    HTTP routing, table registry, WebSocket protocol glue
  net.cpp/.h    epoll event loop, HTTP parsing, RFC 6455 WebSocket
  game.cpp/.h   table state machine: betting, blinds, timers, run-twice,
                discards, showdown, history, ledger
  poker.cpp/.h  deck + 5-card evaluator (hold'em/Omaha/Greek/shortdeck, hi/lo)
  json.h        minimal JSON parser/writer
  util.h        sha1/base64/RNG/time helpers
public/
  index.html, style.css, app.js   the frontend (no framework)
test/
  test.py      end-to-end flow: create/approve/play/chat/history/ledger
  test2.py     scenarios: pineapple, run-twice, timebank, no/extra river,
               shortdeck, side pots
  test3.py     cross-checks showdown winners against an independent
               Python hand evaluator for every variant
  test8.py     hold'em multi-card/discard variants, host transfer, negative chips
  test9.py     host disconnect election and closed-table ledger
deploy/
  pokervariants.service           systemd unit
  nginx-pokervariants.conf        Nginx WebSocket reverse proxy
DEPLOY.md                         Namecheap VPS deployment guide
```

## Protocol

Clients connect to `ws://host/ws/<tableId>` (the creator appends `?k=<hostKey>`). Every welcome
message contains a per-player resume token; sending it back on reconnect reattaches the same
player. Messages are JSON: the client sends `join`, `watch`, `act`, `discard`, `chat`,
`set` (host), `approve`/`reject` (host), `chips` (host, positive or negative), `setstack` (host),
`transfer` (host), `remove` (host), `rebuy`, `stand`, `move`, `run2`, `hist`; the server sends
`welcome`, `state` (full
snapshot incl. your hole cards and action bounds), `chat`, `hand` (finished-hand summary),
`history`, `err`. `GET /api/table/<id>` returns the table ID, closed flag and ledger, which the
frontend uses to render a closed table without joining it.
