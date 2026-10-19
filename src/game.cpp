#include "game.h"
#include "util.h"
#include <algorithm>

bool gVerbose = false;

void Table::sfx(const std::string& s, int n, Player* to) {
  auto j = J::obj();
  j->o["t"] = J::str("sfx");
  j->o["s"] = J::str(s);
  if (n > 0) j->o["n"] = J::number(n);
  std::string msg = J::dump(j);
  if (to) { if (to->conn) net->sendText(to->conn, msg); }
  else broadcast(msg);
}

void Table::logAct(int street, Player* p, const std::string& a, int amt) {
  auto j = J::obj();
  j->o["s"] = J::number(street);
  j->o["name"] = J::str(p->name);
  j->o["a"] = J::str(a);
  j->o["amt"] = J::number(amt);
  handLog.push_back(j);
}

void Table::computeEquity() {
  equities.clear();
  auto nf = nonFolded();
  if (nf.size() < 2) return;
  int toCome = handSt.boardCards() - (int)board.size();
  if (toCome <= 0) return;
  std::vector<Card> deck = deckCards;
  if ((int)deck.size() < toCome) return;
  int samples = handSt.game == 0 ? 20000 : 4000;
  std::mt19937_64 g((uint64_t)rng());
  std::map<int, double> acc;
  std::vector<Card> b;
  for (int s = 0; s < samples; s++) {
    std::shuffle(deck.begin(), deck.end(), g);
    b = board;
    for (int i = 0; i < toCome; i++) b.push_back(deck[i]);
    uint64_t best = 0;
    std::vector<Player*> win;
    for (Player* p : nf) {
      uint64_t k = bestFor(p, b);
      if (win.empty() || k > best) { best = k; win = {p}; }
      else if (k == best) win.push_back(p);
    }
    double hiShare = handSt.hilo ? 0.5 : 1.0;
    for (Player* p : win) acc[p->id] += hiShare / win.size();
    if (handSt.hilo) {
      uint64_t bl = UINT64_MAX;
      std::vector<Player*> lo;
      for (Player* p : nf) {
        uint64_t k;
        if (lowFor(p, b, k)) {
          if (lo.empty() || k < bl) { bl = k; lo = {p}; }
          else if (k == bl) lo.push_back(p);
        }
      }
      if (!lo.empty()) { for (Player* p : lo) acc[p->id] += 0.5 / lo.size(); }
      else { for (Player* p : win) acc[p->id] += 0.5 / win.size(); }
    }
  }
  for (Player* p : nf) equities[p->id] = acc[p->id] / samples;
}

static std::string chipsStr(int cents) {
  int whole = cents / 100;
  int frac = cents % 100;
  if (frac == 0) return std::to_string(whole);
  char buf[32];
  snprintf(buf, sizeof buf, "%d.%02d", whole, frac);
  std::string s = buf;
  if (!s.empty() && s[s.size() - 1] == '0') s.pop_back();
  return s;
}

void Settings::sanitize() {
  if (sb < 1) sb = 1;
  if (bb < 1) bb = 1;
  if (bb < sb) bb = sb;
  if (sec < 5) sec = 5;
  if (sec > 300) sec = 300;
  if (game == 0) {
    if (cards < 2) cards = 2;
    if (cards > 6) cards = 6;
    hilo = false;
    if (cards != 2) greek = false;
    if (cards < 3) { pineapple = false; irish = false; }
  } else {
    if (cards < 3) cards = 3;
    if (cards > 6) cards = 6;
    greek = false;
  }
  if (shortdeck) hilo = false;
  if (pineapple && irish) irish = false;
  if (noRiver) extraRiver = false;
}

int Settings::maxPlayers() const {
  int deck = shortdeck ? 36 : 52;
  int reserve = boardCards();
  if (reserve < 5) reserve = 5;
  int per = cards;
  int m = (deck - reserve) / per;
  if (m < 2) m = 2;
  if (m > 10) m = 10;
  return m;
}

std::string Settings::variant() const {
  std::string s = game == 0 ? "Hold'em" : "Omaha";
  s += bet == 0 ? " NL" : " PL";
  if (game == 0) {
    if (cards > 2) s += " " + std::to_string(cards) + "-card";
    if (greek) s += " Greek";
    if (pineapple) s += " Pineapple";
    if (irish) s += " Irish";
  } else {
    s += " " + std::to_string(cards) + "-card";
    if (pineapple) s += " Pineapple";
    if (irish) s += " Irish";
    if (hilo) s += " Hi/Lo";
  }
  if (shortdeck) s += " Shortdeck";
  if (noRiver) s += " No-River";
  if (extraRiver) s += " Extra-River";
  return s;
}

J::Ptr Settings::toJson() const {
  auto j = J::obj();
  j->o["sb"] = J::number(sb);
  j->o["bb"] = J::number(bb);
  j->o["runTwice"] = J::boolean(runTwice);
  j->o["revealAllIn"] = J::boolean(revealAllIn);
  j->o["game27"] = J::boolean(game27);
  j->o["value27"] = J::number(value27);
  j->o["nitGame"] = J::boolean(nitGame);
  j->o["nitValue"] = J::number(nitValue);
  j->o["straddle"] = J::boolean(straddle);
  j->o["sec"] = J::number(sec);
  j->o["paused"] = J::boolean(paused);
  j->o["game"] = J::number(game);
  j->o["bet"] = J::number(bet);
  j->o["greek"] = J::boolean(greek);
  j->o["pineapple"] = J::boolean(pineapple);
  j->o["irish"] = J::boolean(irish);
  j->o["shortdeck"] = J::boolean(shortdeck);
  j->o["cards"] = J::number(cards);
  j->o["hilo"] = J::boolean(hilo);
  j->o["noRiver"] = J::boolean(noRiver);
  j->o["extraRiver"] = J::boolean(extraRiver);
  j->o["maxPlayers"] = J::number(maxPlayers());
  j->o["v"] = J::str(variant());
  return j;
}

Table::Table(const std::string& tableId, const std::string& hk, Net* n)
    : id(tableId), hostKey(hk), net(n) {
  std::random_device rd;
  rng.seed(rd() ^ (uint64_t)nowMs());
}

Player* Table::getP(int id) {
  for (auto& p : players) if (p->id == id) return p.get();
  return nullptr;
}

Player* Table::bySeat(int s) {
  for (auto& p : players) if (p->seated && p->seat == s) return p.get();
  return nullptr;
}

int Table::seatedCount() const {
  int n = 0;
  for (auto& p : players) if (p->seated) n++;
  return n;
}

int Table::connectedCount() const {
  int n = 0;
  for (auto& p : players) if (p->conn) n++;
  return n;
}

bool Table::hasHost() const {
  for (auto& p : players) if (p->host) return true;
  return false;
}

Player* Table::randomOnlinePlayer(Player* exclude) {
  std::vector<Player*> seated, online;
  for (auto& p : players) {
    if (!p->conn || p.get() == exclude) continue;
    online.push_back(p.get());
    if (p->seated) seated.push_back(p.get());
  }
  std::vector<Player*>& pool = seated.empty() ? online : seated;
  if (pool.empty()) return nullptr;
  std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
  return pool[dist(rng)];
}

void Table::makeHost(Player* target, Player* by) {
  if (!target || !target->conn || target->host) return;
  for (auto& p : players) p->host = false;
  target->host = true;
  hostKey = randHex(16);
  if (by) sysMsg(by->name + " transferred host to " + target->name);
  else sysMsg(target->name + " is now the host");
  sendState();
}

void Table::electNewHost() {
  if (closed) return;
  Player* target = randomOnlinePlayer(nullptr);
  if (target) makeHost(target, nullptr);
}

void Table::settleHostPresence() {
  if (closed) return;
  for (auto& p : players) if (p->host && p->conn) return;
  electNewHost();
}

void Table::closeTable(int64_t now) {
  closed = true;
  closedAt = now;
  emptySince = 0;
  st.paused = true;
  pending.clear();
  handActive = false;
  phase = 5;
  toActId = 0;
  turnDeadline = 0;
  nextDealAt = 0;
  revealAt = 0;
  equities.clear();
  dq.clear();
  dqIdx = 0;
}

std::vector<Player*> Table::seatsInOrder(int fromSeat, const std::function<bool(Player*)>& pred) {
  std::vector<Player*> out;
  for (int i = 1; i <= 10; i++) {
    int s = (fromSeat + i) % 10;
    Player* p = bySeat(s);
    if (p && pred(p)) out.push_back(p);
  }
  return out;
}

std::vector<Player*> Table::inHandOrder() {
  return seatsInOrder(dealerSeat, [](Player* p) { return p->inHand; });
}

std::vector<Player*> Table::nonFolded() {
  std::vector<Player*> out;
  for (auto& p : players) if (p->inHand && !p->folded) out.push_back(p.get());
  return out;
}

std::vector<Player*> Table::active() {
  std::vector<Player*> out;
  for (auto& p : players) if (p->inHand && !p->folded && !p->allIn) out.push_back(p.get());
  return out;
}

bool Table::hasMoreStreets() const {
  if (street == 0) return true;
  if (street == 1) return true;
  if (street == 2) return !handSt.noRiver;
  if (street == 3) return handSt.extraRiver;
  return false;
}

int Table::totalPot() const {
  int t = 0;
  for (auto& p : players) if (p->inHand) t += p->totalComm;
  return t;
}

int Table::displayPot() const {
  int total = totalPot();
  int streetTotal = 0;
  for (auto& p : players) if (p->inHand) streetTotal += p->streetBet;
  int base = total - streetTotal;
  if (street == 0) base += postedSB + postedBB + postedStraddle;
  return base;
}

void Table::broadcast(const std::string& s) {
  for (auto& p : players) if (p->conn) net->sendText(p->conn, s);
}

void Table::sendTo(Player* p, const std::string& s) {
  if (p && p->conn) net->sendText(p->conn, s);
}

void Table::errTo(Player* p, const std::string& m) {
  auto j = J::obj();
  j->o["t"] = J::str("err");
  j->o["m"] = J::str(m);
  sendTo(p, J::dump(j));
}

void Table::sysMsg(const std::string& m) {
  auto j = J::obj();
  j->o["t"] = J::str("chat");
  j->o["id"] = J::number(0);
  j->o["name"] = J::str("system");
  j->o["m"] = J::str(m);
  auto keep = J::obj();
  keep->o["name"] = J::str("system");
  keep->o["m"] = J::str(m);
  chatRec.push_back(keep);
  while (chatRec.size() > 50) chatRec.pop_front();
  broadcast(J::dump(j));
}

Player* Table::addPlayer(Conn* c) {
  auto p = std::make_unique<Player>();
  p->id = (int)players.size() + 1;
  p->conn = c;
  p->token = randHex(16);
  p->host = c->isHostConn && !hasHost();
  Player* raw = p.get();
  players.push_back(std::move(p));
  return raw;
}

Player* Table::reattachHost(Conn* c) {
  for (auto& p : players) {
    if (p->host && !p->conn) {
      p->conn = c;
      return p.get();
    }
  }
  return nullptr;
}

Player* Table::reattachToken(Conn* c, const std::string& token) {
  if (token.empty()) return nullptr;
  for (auto& p : players) {
    if (p->token == token && !p->conn) {
      p->conn = c;
      return p.get();
    }
  }
  return nullptr;
}

void Table::onDisconnect(Conn* c, int playerId) {
  Player* p = getP(playerId);
  if (!p) return;
  if (p->conn != c) return;
  p->conn = nullptr;
  settleHostPresence();
  if (p->r2Asked && !p->r2Answered) {
    p->r2Answered = true;
    p->r2Ans = false;
    if (phase == 3) resolveRun2();
  }
  if (phase == 6 && toActId == playerId) applyStraddle(p, false);
  if (phase == 1 && toActId == playerId) autoAct(p);
  if (phase == 2 && toActId == playerId) autoDiscard(p);
  sendState();
}

void Table::seat(Player* p, int stack, int prefSeat) {
  if (p->seated) { errTo(p, "already seated"); return; }
  if (stack <= 0) stack = handActive ? st.bb * 200 : st.bb * 100;
  if (seatedCount() >= st.maxPlayers()) { errTo(p, "table is full for this variant"); return; }
  int s = -1;
  if (prefSeat >= 0 && prefSeat < 10 && !bySeat(prefSeat)) s = prefSeat;
  if (s < 0) {
    for (int i = 0; i < 10; i++) {
      if (!bySeat(i)) { s = i; break; }
    }
  }
  if (s < 0) { errTo(p, "table is full"); return; }
  p->seat = s;
  p->seated = true;
  p->stack = stack;
  p->boughtIn += stack;
  p->bank = 60;
}

void Table::unseat(Player* p) {
  if (p->inHand) {
    p->folded = true;
    if (phase == 1 && toActId == p->id) {
      auto nf = nonFolded();
      if (nf.size() <= 1) {
        toActId = 0;
        endStreet();
      } else {
        nextToAct();
      }
    } else if (phase == 1) {
      auto nf = nonFolded();
      if (nf.size() <= 1) { toActId = 0; endStreet(); }
    } else if (phase == 2) {
      dq.erase(std::remove(dq.begin(), dq.end(), p), dq.end());
      if (dqIdx >= (int)dq.size()) {
        dqIdx = 0;
        phase = 1;
        finalizeRun2OrDeal();
      }
    }
  }
  p->seated = false;
  p->seat = -1;
}

void Table::maybeDeal() {
  if (handActive || st.paused) return;
  if (nextDealAt > nowMs()) return;
  int eligible = 0, seated = 0;
  for (auto& p : players) {
    if (p->seated) seated++;
    if (p->seated && p->conn && p->stack > 0 && !p->sittingOut) eligible++;
  }
  if (eligible < 2) return;
  if (seated > st.maxPlayers()) return;
  dealHand();
}

void Table::dealHand() {
  if (pendingSettingsValid) { st = pendingSettings; pendingSettingsValid = false; }
  auto elig = seatsInOrder(dealerSeat, [](Player* p) { return p->seated && p->conn && p->stack > 0 && !p->sittingOut; });
  if (elig.size() < 2) return;
  dealerSeat = elig[0]->seat;
  hId = ++handCount;
  handActive = true;
  handSt = st;
  street = 0;
  currentBet = 0;
  lastRaise = handSt.bb;
  board.clear();
  boardB.clear();
  run2 = false;
  pendingRun2 = false;
  run2Offered = false;
  revealAt = 0;
  nextDealAt = 0;
  equities.clear();
  handLog.clear();
  rabbitCards.clear();
  postedSB = 0;
  postedBB = 0;
  postedStraddle = 0;
  straddleActive = false;
  straddleSeat = -1;
  straddlePlayerId = 0;
  phase = 0;
  toActId = 0;
  turnDeadline = 0;
  for (auto& p : players) {
    p->inHand = false; p->folded = false; p->allIn = false; p->acted = false; p->show = false;
    p->streetBet = 0; p->totalComm = 0; p->hole.clear();
    p->r2Asked = false; p->r2Answered = false; p->r2Ans = false;
    p->showChoice = false;
    p->wonThisHand = false;
  }
  for (Player* p : elig) {
    p->inHand = true;
    p->stackStart = p->stack;
    p->bank = std::min(120, p->bank + 15);
  }
  if (handSt.straddle && elig.size() >= 3) {
    auto in = seatsInOrder(dealerSeat, [](Player* p) { return p->inHand; });
    if (in.size() >= 3) {
      Player* utg = in[2];
      if (utg->stack >= 2 * handSt.bb) {
        straddleSeat = utg->seat;
        phase = 6;
        toActId = utg->id;
        turnPhase = 0;
        turnWindow = (int64_t)handSt.sec * 1000;
        turnDeadline = nowMs() + (int64_t)handSt.sec * 1000;
        sendState();
        return;
      }
    }
  }
  startHandDeal();
}

void Table::startHandDeal() {
  phase = 0;
  toActId = 0;
  turnDeadline = 0;
  Deck d;
  d.build(handSt.shortdeck);
  d.shuffle(rng);
  deckCards = d.cs;
  std::vector<Player*> elig;
  for (auto& p : players) if (p->inHand) elig.push_back(p.get());
  int nh = handSt.cards;
  int ti = 0;
  for (Player* p : elig) {
    for (int i = 0; i < nh; i++) {
      if (ti < (int)deckCards.size()) p->hole.push_back(deckCards[ti++]);
    }
  }
  deckCards.erase(deckCards.begin(), deckCards.begin() + ti);
  if (gVerbose) {
    printf("=== hand #%d === dealer seat %d, blinds %s/%s\n", hId, dealerSeat,
           chipsStr(handSt.sb).c_str(), chipsStr(handSt.bb).c_str());
    for (Player* p : elig) {
      std::string hs;
      for (auto& c : p->hole) { if (!hs.empty()) hs += " "; hs += c.str(); }
      printf("  %-12s %s\n", p->name.c_str(), hs.c_str());
    }
    int bc = handSt.boardCards();
    std::string bd;
    for (int i = 0; i < bc && i < (int)deckCards.size(); i++) {
      if (i) bd += " ";
      bd += deckCards[deckCards.size() - 1 - i].str();
    }
    printf("  board to come: %s\n", bd.c_str());
    printf("  stacks:");
    for (Player* p : elig) printf(" %s=%s", p->name.c_str(), chipsStr(p->stack).c_str());
    printf("\n");
  }
  if (nh > 0) sfx("deal", nh, nullptr);
  postBlinds();
  if (straddleActive) {
    Player* s = getP(straddlePlayerId);
    if (s && s->inHand) {
      int a = std::min(2 * handSt.bb, s->stack);
      s->stack -= a; s->streetBet += a; s->totalComm += a;
      postedStraddle = a;
      if (s->stack == 0) s->allIn = true;
      logAct(0, s, "straddle", a);
    }
  }
  beginStreet();
}

void Table::applyStraddle(Player* p, bool yes) {
  if (phase != 6) return;
  straddlePlayerId = toActId;
  if (p && yes) straddleActive = true;
  startHandDeal();
}

void Table::postBlinds() {
  auto in = inHandOrder();
  Player *sb = nullptr, *bb = nullptr;
  if (in.size() == 2) {
    sb = bySeat(dealerSeat);
    bb = in[0] == sb ? in[1] : in[0];
  } else if (in.size() >= 3) {
    sb = in[0];
    bb = in[1];
  }
  if (sb) {
    sbSeat = sb->seat;
    int a = std::min(handSt.sb, sb->stack);
    sb->stack -= a; sb->streetBet = a; sb->totalComm += a;
    postedSB = a;
    if (sb->stack == 0) sb->allIn = true;
    logAct(0, sb, "small blind", a);
  }
  if (bb) {
    bbSeat = bb->seat;
    int a = std::min(handSt.bb, bb->stack);
    bb->stack -= a; bb->streetBet = a; bb->totalComm += a;
    postedBB = a;
    if (bb->stack == 0) bb->allIn = true;
    logAct(0, bb, "big blind", a);
  }
}

void Table::beginStreet() {
  for (auto& p : players) {
    if (!p->inHand) continue;
    p->acted = false;
    if (street != 0) p->streetBet = 0;
  }
  currentBet = 0;
  lastRaise = handSt.bb;
  if (street == 0) {
    for (auto& p : players) if (p->inHand) currentBet = std::max(currentBet, p->streetBet);
  }
  toActId = 0;
  phase = 1;
  nextToAct();
}

bool Table::needsAction(Player* p) const {
  return p->inHand && !p->folded && !p->allIn && (!p->acted || p->streetBet < currentBet);
}

void Table::nextToAct() {
  auto act = active();
  if (act.size() == 1 && nonFolded().size() > 1 && act[0]->streetBet >= currentBet) {
    toActId = 0;
    endStreet();
    return;
  }
  Player* cur = getP(toActId);
  int from = cur ? cur->seat : (street == 0 ? (straddleActive ? straddleSeat : bbSeat) : dealerSeat);
  for (int i = 1; i <= 10; i++) {
    int s = (from + i) % 10;
    Player* p = bySeat(s);
    if (p && needsAction(p)) {
      toActId = p->id;
      phase = 1;
      turnPhase = 0;
      turnWindow = (int64_t)handSt.sec * 1000;
      turnDeadline = nowMs() + (int64_t)handSt.sec * 1000;
      sendState();
      return;
    }
  }
  toActId = 0;
  endStreet();
}

void Table::endStreet() {
  auto nf = nonFolded();
  if (nf.size() <= 1) { showdown(); return; }
  bool allAllIn = true;
  for (Player* p : nf) if (!p->allIn) allAllIn = false;
  pendingRun2 = !run2Offered && st.runTwice && nf.size() == 2 && allAllIn && hasMoreStreets();
  if (pendingRun2) {
    int remaining = handSt.boardCards() - (int)board.size();
    if (deckCards.size() < (size_t)(remaining * 2)) pendingRun2 = false;
  }
  if (handSt.pineapple && street == 0) { startDiscard(); return; }
  if (handSt.irish && street == 1) { startDiscard(); return; }
  finalizeRun2OrDeal();
}

void Table::finalizeRun2OrDeal() {
  if (pendingRun2 && nonFolded().size() == 2 && hasMoreStreets()) {
    offerRun2();
    return;
  }
  if (allInPacing()) {
    if (handSt.revealAllIn) {
      for (Player* p : nonFolded()) p->show = true;
      computeEquity();
    }
    revealAt = nowMs() + 2500;
    sendState();
    return;
  }
  if (hasMoreStreets()) { dealNextStreet(); return; }
  showdown();
}

bool Table::allInPacing() {
  auto nf = nonFolded();
  if (nf.size() < 2) return false;
  int withChips = 0;
  for (Player* p : nf) if (!p->allIn) withChips++;
  return withChips <= 1;
}

void Table::dealBoardCard() {
  if (!deckCards.empty()) {
    board.push_back(deckCards.back());
    deckCards.pop_back();
    sfx("deal", 1, nullptr);
  }
  if (run2 && !deckCards.empty()) {
    boardB.push_back(deckCards.back());
    deckCards.pop_back();
    sfx("deal", 1, nullptr);
  }
}

void Table::dealNextStreet() {
  street++;
  if (street == 1) { dealBoardCard(); dealBoardCard(); dealBoardCard(); }
  else dealBoardCard();
  if (gVerbose) {
    std::string b;
    for (auto& c : board) { if (!b.empty()) b += " "; b += c.str(); }
    printf("  board: %s\n", b.c_str());
  }
  phase = 1;
  beginStreet();
}

void Table::startDiscard() {
  dq.clear();
  auto nf = nonFolded();
  std::sort(nf.begin(), nf.end(), [](Player* a, Player* b) { return a->seat < b->seat; });
  for (Player* p : nf) if ((int)p->hole.size() > 2) dq.push_back(p);
  dqIdx = 0;
  if (dq.empty()) {
    phase = 1;
    finalizeRun2OrDeal();
    return;
  }
  phase = 2;
  toActId = dq[0]->id;
  turnPhase = 0;
  turnWindow = (int64_t)handSt.sec * 1000;
  turnDeadline = nowMs() + (int64_t)handSt.sec * 1000;
  sendState();
}

void Table::applyDiscard(Player* p, const std::vector<int>& idx) {
  if (phase != 2 || p->id != toActId) return;
  int need = (int)p->hole.size() - 2;
  if ((int)idx.size() != need) { errTo(p, "must discard exactly " + std::to_string(need)); return; }
  std::vector<char> keep(p->hole.size(), 1);
  for (int i : idx) {
    if (i < 0 || i >= (int)p->hole.size() || !keep[i]) { errTo(p, "bad discard"); return; }
    keep[i] = 0;
  }
  std::vector<Card> nh;
  for (size_t i = 0; i < p->hole.size(); i++) if (keep[i]) nh.push_back(p->hole[i]);
  p->hole = nh;
  dqIdx++;
  if (dqIdx >= (int)dq.size()) {
    dqIdx = 0;
    phase = 1;
    toActId = 0;
    turnDeadline = 0;
    finalizeRun2OrDeal();
    return;
  }
  toActId = dq[dqIdx]->id;
  turnPhase = 0;
  turnWindow = (int64_t)handSt.sec * 1000;
  turnDeadline = nowMs() + (int64_t)handSt.sec * 1000;
  sendState();
}

void Table::autoDiscard(Player* p) {
  int need = (int)p->hole.size() - 2;
  if (need > 0 && (int)p->hole.size() > 2) p->hole.erase(p->hole.begin(), p->hole.begin() + need);
  dqIdx++;
  if (dqIdx >= (int)dq.size()) {
    dqIdx = 0;
    phase = 1;
    toActId = 0;
    turnDeadline = 0;
    finalizeRun2OrDeal();
    return;
  }
  toActId = dq[dqIdx]->id;
  turnPhase = 0;
  turnWindow = (int64_t)handSt.sec * 1000;
  turnDeadline = nowMs() + (int64_t)handSt.sec * 1000;
  sendState();
}

void Table::offerRun2() {
  phase = 3;
  pendingRun2 = true;
  run2Offered = true;
  r2Deadline = nowMs() + (int64_t)handSt.sec * 1000;
  auto nf = nonFolded();
  for (Player* p : nf) {
    p->r2Asked = true;
    p->r2Answered = false;
    p->r2Ans = false;
  }
  sendState();
}

void Table::resolveRun2() {
  if (!pendingRun2) return;
  bool allYes = true, allAnswered = true;
  auto nf = nonFolded();
  for (Player* p : nf) {
    if (!p->r2Asked) { allAnswered = false; continue; }
    if (!p->r2Answered) { allAnswered = false; allYes = false; }
    else if (!p->r2Ans) allYes = false;
  }
  if (!allAnswered) return;
  pendingRun2 = false;
  phase = 1;
  run2 = allYes;
  if (run2) boardB = board;
  finalizeRun2OrDeal();
}

int Table::minRaiseTo(Player* p) const {
  int base = currentBet == 0 ? handSt.bb : currentBet + lastRaise;
  int cap = p->streetBet + p->stack;
  return std::min(base, cap);
}

int Table::maxRaiseTo(Player* p) const {
  int cap = p->streetBet + p->stack;
  if (handSt.bet == 1) {
    int P = totalPot();
    int toCall = std::max(0, currentBet - p->streetBet);
    int maxTo = currentBet + P + toCall;
    return std::min(maxTo, cap);
  }
  return cap;
}

void Table::applyAction(Player* p, const std::string& a, int amt) {
  if (phase != 1 || p->id != toActId) { errTo(p, "not your turn"); return; }
  if (a == "fold") {
    logAct(street, p, "fold", 0);
    doFold(p);
    return;
  }
  if (a == "check") {
    if (p->streetBet < currentBet) { errTo(p, "must call or fold"); return; }
    logAct(street, p, "check", 0);
    p->acted = true;
    nextToAct();
    return;
  }
  if (a == "call") {
    int call = std::min(currentBet - p->streetBet, p->stack);
    p->stack -= call;
    p->streetBet += call;
    p->totalComm += call;
    p->acted = true;
    if (p->stack == 0) p->allIn = true;
    logAct(street, p, p->allIn ? "all-in" : "call", call);
    sfx("bet", 0, nullptr);
    nextToAct();
    return;
  }
  if (a == "bet" || a == "allin") {
    int to = a == "allin" ? p->streetBet + p->stack : amt;
    if (to <= p->streetBet) { errTo(p, "bet too small"); return; }
    if (to == currentBet) {
      int call = currentBet - p->streetBet;
      if (call > p->stack) { errTo(p, "bad call"); return; }
      p->stack -= call;
      p->streetBet += call;
      p->totalComm += call;
      p->acted = true;
      if (p->stack == 0) p->allIn = true;
      logAct(street, p, p->allIn ? "all-in" : "call", call);
      sfx("bet", 0, nullptr);
      nextToAct();
      return;
    }
    if (to > currentBet) {
      int minTo = currentBet == 0 ? handSt.bb : currentBet + lastRaise;
      int maxTo = maxRaiseTo(p);
      if (to > maxTo) { errTo(p, "bet exceeds max"); return; }
      int raiseSize = to - currentBet;
      if (to < minTo) {
        if (to != p->streetBet + p->stack) { errTo(p, "below minimum raise"); return; }
      }
      int add = to - p->streetBet;
      p->stack -= add;
      p->streetBet += add;
      p->totalComm += add;
      p->acted = true;
      if (p->stack == 0) p->allIn = true;
      logAct(street, p, p->allIn ? "all-in" : (currentBet == 0 ? "bet" : "raise"), to);
      sfx("bet", 0, nullptr);
      bool full = currentBet == 0 ? (to >= handSt.bb) : (raiseSize >= lastRaise);
      if (full) {
        lastRaise = raiseSize;
        for (auto& q : players) {
          if (q.get() != p && q->inHand && !q->folded && !q->allIn) q->acted = false;
        }
      }
      currentBet = std::max(currentBet, to);
      nextToAct();
      return;
    }
  }
  errTo(p, "invalid action");
}

void Table::doFold(Player* p) {
  p->folded = true;
  auto nf = nonFolded();
  if (nf.size() <= 1) {
    toActId = 0;
    endStreet();
    return;
  }
  nextToAct();
}

void Table::autoAct(Player* p) {
  if (phase == 1 && toActId == p->id) {
    if (p->streetBet >= currentBet) {
      p->acted = true;
      nextToAct();
    } else {
      doFold(p);
    }
  }
}

void Table::showdown() {
  phase = 4;
  auto nf = nonFolded();
  if (nf.size() > 1) {
    for (Player* p : nf) p->show = true;
  } else if (nf.size() == 1) {
    nf[0]->showChoice = true;
  }
  for (auto& p : players) if (p->inHand && p->folded) p->showChoice = true;
  for (auto& p : players) if (p->inHand) p->streetBet = 0;
  std::map<int, int> win;
  std::vector<int> levels;
  for (auto& p : players) if (p->inHand) levels.push_back(p->totalComm);
  std::sort(levels.begin(), levels.end());
  levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
  int prev = 0;
  for (int level : levels) {
    if (level <= prev) continue;
    int amt = level - prev;
    int cnt = 0;
    for (auto& p : players) if (p->inHand && p->totalComm >= level) cnt++;
    int potAmt = amt * cnt;
    std::vector<Player*> elig;
    for (Player* p : nf) if (p->totalComm >= level) elig.push_back(p);
    if (elig.empty()) {
      for (auto& p : players) if (p->inHand && p->totalComm >= level) win[p->id] += amt;
    } else {
      awardTier(potAmt, elig, win);
    }
    prev = level;
  }
  for (auto& p : players) if (p->inHand) p->stack += win[p->id];
  for (auto& p : players) if (p->inHand && win[p->id] > 0) {
    sfx("win", 0, p.get());
    p->wonHands++;
    p->wonThisHand = true;
  }
  applySideGames(win);
  auto sumPlayers = J::arr();
  for (auto& p : players) {
    if (!p->inHand) continue;
    int w = win[p->id];
    int net = p->stack - p->stackStart;
    auto e = J::obj();
    e->o["id"] = J::number(p->id);
    e->o["name"] = J::str(p->name);
    e->o["seat"] = J::number(p->seat);
    e->o["folded"] = J::boolean(p->folded);
    e->o["allin"] = J::boolean(p->allIn);
    e->o["net"] = J::number(net);
    e->o["won"] = J::number(w);
    if (p->show) {
      auto h = J::arr();
      for (auto& c : p->hole) h->a.push_back(J::str(c.str()));
      e->o["hole"] = h;
    }
    sumPlayers->a.push_back(e);
  }
  auto sum = J::obj();
  sum->o["id"] = J::number(hId);
  sum->o["time"] = J::number((double)nowMs());
  sum->o["variant"] = J::str(handSt.variant());
  sum->o["sb"] = J::number(handSt.sb);
  sum->o["bb"] = J::number(handSt.bb);
  sum->o["run2"] = J::boolean(run2);
  auto b = J::arr();
  for (auto& c : board) b->a.push_back(J::str(c.str()));
  sum->o["board"] = b;
  if (run2) {
    auto b2 = J::arr();
    for (auto& c : boardB) b2->a.push_back(J::str(c.str()));
    sum->o["board2"] = b2;
  }
  sum->o["players"] = sumPlayers;
  {
    auto acts = J::arr();
    for (auto& a : handLog) acts->a.push_back(a);
    sum->o["actions"] = acts;
  }
  lastHand = sum;
  history.push_front(sum);
  while (history.size() > 500) history.pop_back();
  auto m = J::obj();
  m->o["t"] = J::str("hand");
  for (auto& kv : sum->o) m->o[kv.first] = kv.second;
  broadcast(J::dump(m));
  endHand();
}

void Table::awardTier(int amt, const std::vector<Player*>& elig, std::map<int, int>& win) {
  if (run2) {
    int a1 = amt - amt / 2;
    awardTierSingle(a1, elig, board, win);
    awardTierSingle(amt / 2, elig, boardB, win);
  } else {
    awardTierSingle(amt, elig, board, win);
  }
}

void Table::awardTierSingle(int amt, const std::vector<Player*>& elig, const std::vector<Card>& b, std::map<int, int>& win) {
  if (handSt.hilo) {
    std::vector<Player*> hi;
    uint64_t bestHi = 0;
    for (Player* p : elig) {
      uint64_t k = bestFor(p, b);
      if (hi.empty() || k > bestHi) { bestHi = k; hi = {p}; }
      else if (k == bestHi) hi.push_back(p);
    }
    int halfH = amt - amt / 2;
    split(hi, halfH, win);
    std::vector<Player*> lo;
    uint64_t bestLo = UINT64_MAX;
    for (Player* p : elig) {
      uint64_t k;
      if (lowFor(p, b, k)) {
        if (lo.empty() || k < bestLo) { bestLo = k; lo = {p}; }
        else if (k == bestLo) lo.push_back(p);
      }
    }
    if (!lo.empty()) split(lo, amt / 2, win);
    else split(hi, amt / 2, win);
    return;
  }
  std::vector<Player*> w;
  uint64_t best = 0;
  for (Player* p : elig) {
    uint64_t k = bestFor(p, b);
    if (w.empty() || k > best) { best = k; w = {p}; }
    else if (k == best) w.push_back(p);
  }
  split(w, amt, win);
}

void Table::split(const std::vector<Player*>& w, int amt, std::map<int, int>& win) {
  if (w.empty() || amt <= 0) return;
  std::vector<Player*> ws = w;
  std::sort(ws.begin(), ws.end(), [](Player* a, Player* b) { return a->seat < b->seat; });
  int each = amt / (int)ws.size();
  int rem = amt % (int)ws.size();
  for (size_t i = 0; i < ws.size(); i++) win[ws[i]->id] += each + ((int)i < rem ? 1 : 0);
}

uint64_t Table::bestFor(Player* p, const std::vector<Card>& b) {
  if (handSt.game == 0) return bestHoldem(p->hole, b, handSt.greek, handSt.shortdeck);
  return bestOmaha(p->hole, b, handSt.shortdeck);
}

bool Table::is27off(const std::vector<Card>& h) {
  if (h.size() != 2) return false;
  int a = h[0].r, b = h[1].r;
  bool ranks = (a == 2 && b == 7) || (a == 7 && b == 2);
  return ranks && h[0].s != h[1].s;
}

void Table::applySideGames(std::map<int, int>& win) {
  std::vector<Player*> dealt;
  for (auto& p : players) if (p->inHand) dealt.push_back(p.get());
  if (handSt.game27 && handSt.game == 0) {
    for (Player* w : dealt) {
      if (win[w->id] <= 0 || !is27off(w->hole)) continue;
      for (Player* q : dealt) {
        if (q == w) continue;
        int v = std::min(handSt.value27, std::max(0, q->stack));
        q->stack -= v;
        w->stack += v;
      }
      sysMsg(w->name + " wins the 2-7 bounty");
    }
  }
  if (handSt.nitGame) {
    Player* hw = nullptr;
    int best = 0;
    for (Player* w : dealt) if (win[w->id] > best) { best = win[w->id]; hw = w; }
    if (hw) {
      Player* nit = getP(nitId);
      if (nit && nit->inHand && nit != hw) {
        for (Player* q : dealt) {
          if (q == nit) continue;
          int v = std::min(handSt.nitValue, std::max(0, nit->stack));
          nit->stack -= v;
          q->stack += v;
        }
        sysMsg(nit->name + " pays the nit game");
      }
      nitId = hw->id;
    }
  }
}

bool Table::lowFor(Player* p, const std::vector<Card>& b, uint64_t& k) {
  if (!handSt.hilo) return false;
  return bestLowOmaha(p->hole, b, k);
}

void Table::endHand() {
  handActive = false;
  phase = 5;
  pendingRun2 = false;
  revealAt = 0;
  equities.clear();
  toActId = 0;
  turnDeadline = 0;
  dq.clear();
  dqIdx = 0;
  int seated = 0, eligible = 0;
  for (auto& p : players) {
    if (p->seated) seated++;
    if (p->seated && p->conn && p->stack > 0) eligible++;
  }
  if (seated >= 2 && eligible < 2) {
    sysMsg("Waiting for players to rebuy before the next hand");
  }
  nextDealAt = nowMs() + 3000;
  sendState();
  if (gVerbose) {
    printf("  stacks:");
    for (auto& p : players) {
      if (!p->seated) continue;
      printf(" %s=%s%s", p->name.c_str(), chipsStr(p->stack).c_str(), p->wonThisHand ? "*" : "");
    }
    printf("\n\n");
  }
}

void Table::tick(int64_t now) {
  if (closed) return;
  if (connectedCount() == 0) {
    if (emptySince == 0) emptySince = now;
    else if (!handActive && now - emptySince >= 60000) { closeTable(now); return; }
  } else {
    emptySince = 0;
  }
  if (phase == 6) {
    if (now > turnDeadline) {
      Player* p = getP(toActId);
      turnDeadline = 0;
      applyStraddle(p, false);
    }
    return;
  }
  if (revealAt > 0 && now >= revealAt) {
    revealAt = 0;
    if (hasMoreStreets()) dealNextStreet();
    else showdown();
    return;
  }
  if (!handActive && nextDealAt > 0 && now >= nextDealAt) {
    nextDealAt = 0;
    maybeDeal();
  }
  if (phase == 1 || phase == 2) {
    if (toActId > 0) {
      Player* p = getP(toActId);
      if (!p) return;
      if (!p->conn) {
        if (phase == 1) autoAct(p);
        else autoDiscard(p);
        return;
      }
      if (turnPhase == 0 && now > turnDeadline) {
        if (p->bank < 1) {
          if (phase == 1) {
            sysMsg(p->name + " timed out and folded");
            doFold(p);
          } else {
            autoDiscard(p);
          }
          return;
        }
        int grant = std::min(handSt.sec, p->bank);
        p->bank -= grant;
        turnWindow = (int64_t)grant * 1000;
        turnDeadline = now + (int64_t)grant * 1000;
        turnPhase = 1;
        sendState();
      } else if (turnPhase == 1 && now > turnDeadline) {
        if (phase == 1) {
          sysMsg(p->name + " ran out of time bank and folded");
          doFold(p);
        } else {
          autoDiscard(p);
        }
      }
    }
  }
  if (phase == 3 && now > r2Deadline) {
    for (auto& p : players) {
      if (p->r2Asked && !p->r2Answered) { p->r2Answered = true; p->r2Ans = false; }
    }
    resolveRun2();
  }
}

J::Ptr Table::stateJson(int forId) {
  Player* me = getP(forId);
  auto j = J::obj();
  j->o["t"] = J::str("state");
  j->o["id"] = J::str(id);
  j->o["closed"] = J::boolean(closed);
  int hostId = 0;
  for (auto& p : players) if (p->host) hostId = p->id;
  j->o["host"] = J::number(hostId);
  j->o["settings"] = st.toJson();
  if (pendingSettingsValid) j->o["pendingSettings"] = pendingSettings.toJson();
  j->o["maxPlayers"] = J::number(st.maxPlayers());
  j->o["seatedCount"] = J::number(seatedCount());
  j->o["nit"] = J::number(nitId);

  auto pl = J::arr();
  for (auto& p : players) {
    auto e = J::obj();
    e->o["id"] = J::number(p->id);
    e->o["name"] = J::str(p->name);
    e->o["seat"] = J::number(p->seat);
    e->o["stack"] = J::number(p->stack);
    e->o["bought"] = J::number(p->boughtIn);
    e->o["host"] = J::boolean(p->host);
    e->o["connected"] = J::boolean(p->conn != nullptr);
    e->o["seated"] = J::boolean(p->seated);
    e->o["bank"] = J::number(p->bank);
    std::string state = "waiting";
    if (p->inHand) state = p->folded ? "folded" : (p->allIn ? "allin" : "in");
    e->o["state"] = J::str(state);
    e->o["bet"] = J::number(p->streetBet);
    if (p->inHand) e->o["n"] = J::number((int)p->hole.size());
    {
      auto eq = equities.find(p->id);
      if (eq != equities.end()) e->o["eq"] = J::number((double)((int)(eq->second * 1000 + 0.5)) / 10.0);
    }
    if ((p->id == forId || p->show) && !p->hole.empty()) {
      auto h = J::arr();
      for (auto& c : p->hole) h->a.push_back(J::str(c.str()));
      e->o["hole"] = h;
    }
    e->o["show"] = J::boolean(p->show);
    e->o["sitout"] = J::boolean(p->sittingOut);
    e->o["wonhand"] = J::number(p->wonHands);
    pl->a.push_back(e);
  }
  j->o["players"] = pl;

  auto pend = J::arr();
  for (auto& r : pending) {
    auto e = J::obj();
    e->o["id"] = J::number(r.id);
    e->o["name"] = J::str(r.name);
    e->o["stack"] = J::number(r.stack);
    e->o["type"] = J::number(r.type);
    pend->a.push_back(e);
  }
  j->o["pending"] = pend;

  auto h = J::obj();
  h->o["id"] = J::number(hId);
  h->o["street"] = J::number(street);
  const char* ph = phase == 0 ? "idle" : (phase == 1 ? "betting" : (phase == 2 ? "discard" : (phase == 3 ? "run2" : (phase == 4 ? "showdown" : (phase == 6 ? "straddle" : "over")))));
  h->o["phase"] = J::str(ph);
  h->o["pot"] = J::number(displayPot());
  h->o["dealer"] = J::number(dealerSeat);
  h->o["sbSeat"] = J::number(sbSeat);
  h->o["bbSeat"] = J::number(bbSeat);
  h->o["toAct"] = J::number(toActId);
  h->o["sb"] = J::number(handActive ? handSt.sb : st.sb);
  h->o["bb"] = J::number(handActive ? handSt.bb : st.bb);
  if (!handActive && nextDealAt > 0) h->o["nextDeal"] = J::number((double)nextDealAt);
  auto b = J::arr();
  for (auto& c : board) b->a.push_back(J::str(c.str()));
  h->o["board"] = b;
  if (run2) {
    auto b2 = J::arr();
    for (auto& c : boardB) b2->a.push_back(J::str(c.str()));
    h->o["board2"] = b2;
  }
  if (!rabbitCards.empty()) {
    auto r = J::arr();
    for (auto& c : rabbitCards) r->a.push_back(J::str(c.str()));
    h->o["rabbit"] = r;
  }
  if (!handActive && rabbitCards.empty() && board.size() < (size_t)handSt.boardCards()) {
    h->o["canRabbit"] = J::boolean(true);
  }
  if (phase == 3) {
    auto r2 = J::obj();
    auto asked = J::arr();
    auto answers = J::obj();
    for (auto& p : players) {
      if (p->r2Asked) {
        asked->a.push_back(J::number(p->id));
        if (p->r2Answered) answers->o[std::to_string(p->id)] = J::boolean(p->r2Ans);
      }
    }
    r2->o["asked"] = asked;
    r2->o["answers"] = answers;
    r2->o["deadline"] = J::number((double)r2Deadline);
    h->o["run2"] = r2;
  }
  j->o["hand"] = h;
  j->o["last"] = lastHand;

  auto turns = J::arr();
  if ((phase == 1 || phase == 2) && toActId > 0) {
    auto t = J::obj();
    t->o["p"] = J::number(toActId);
    t->o["deadline"] = J::number((double)turnDeadline);
    t->o["phase"] = J::str(turnPhase == 0 ? "main" : "bank");
    t->o["window"] = J::number((double)turnWindow);
    turns->a.push_back(t);
  }
  j->o["turns"] = turns;

  j->o["ledger"] = ledgerJson();

  auto ch = J::arr();
  for (auto& c : chatRec) ch->a.push_back(c);
  j->o["chat"] = ch;

  if (me) {
    auto m = J::obj();
    m->o["id"] = J::number(me->id);
    m->o["name"] = J::str(me->name);
    m->o["host"] = J::boolean(me->host);
    if (me->host) m->o["hostKey"] = J::str(hostKey);
    m->o["seated"] = J::boolean(me->seated);
    m->o["seat"] = J::number(me->seat);
    m->o["stack"] = J::number(me->stack);
    m->o["bank"] = J::number(me->bank);
    bool pend = false;
    for (auto& r : pending) if (r.playerId == me->id) pend = true;
    m->o["pending"] = J::boolean(pend);
    m->o["myTurn"] = J::boolean((phase == 1 || phase == 2) && toActId == me->id);
    if (!me->hole.empty() && me->inHand) {
      auto hh = J::arr();
      for (auto& c : me->hole) hh->a.push_back(J::str(c.str()));
      m->o["cards"] = hh;
    }
    if (phase == 1 && toActId == me->id) {
      m->o["toCall"] = J::number(std::min(currentBet - me->streetBet, me->stack));
      m->o["canCheck"] = J::boolean(me->streetBet >= currentBet);
      m->o["minRaise"] = J::number(minRaiseTo(me));
      m->o["maxRaise"] = J::number(maxRaiseTo(me));
      int P = totalPot();
      int C = std::min(currentBet - me->streetBet, me->stack);
      m->o["halfPot"] = J::number(std::min(maxRaiseTo(me), C + (P + C) / 2));
      m->o["threePot"] = J::number(std::min(maxRaiseTo(me), C + (3 * (P + C)) / 4));
      m->o["fullPot"] = J::number(std::min(maxRaiseTo(me), C + (P + C)));
      m->o["deadline"] = J::number((double)turnDeadline);
      m->o["phase"] = J::str(turnPhase == 0 ? "main" : "bank");
      m->o["window"] = J::number((double)turnWindow);
    }
    if (me->showChoice) m->o["showChoice"] = J::boolean(true);
    if (me->sittingOut) m->o["sitout"] = J::boolean(true);
    if (phase == 6 && toActId == me->id) {
      m->o["straddleChoice"] = J::boolean(true);
      m->o["deadline"] = J::number((double)turnDeadline);
      m->o["window"] = J::number((double)turnWindow);
    }
    if (phase == 2 && toActId == me->id) {
      m->o["discard"] = J::number((int)me->hole.size() - 2);
      m->o["deadline"] = J::number((double)turnDeadline);
      m->o["phase"] = J::str(turnPhase == 0 ? "main" : "bank");
      m->o["window"] = J::number((double)turnWindow);
    }
    if (me->r2Asked) {
      m->o["run2Asked"] = J::boolean(true);
      m->o["run2Answered"] = J::boolean(me->r2Answered);
    }
    j->o["me"] = m;
  }
  return j;
}

J::Ptr Table::ledgerJson() const {
  auto led = J::arr();
  for (auto& p : players) {
    if (p->boughtIn <= 0 && !p->seated) continue;
    auto e = J::obj();
    e->o["id"] = J::number(p->id);
    e->o["name"] = J::str(p->name);
    e->o["bought"] = J::number(p->boughtIn);
    e->o["stack"] = J::number(p->stack);
    e->o["wl"] = J::number(p->stack - p->boughtIn);
    e->o["seated"] = J::boolean(p->seated);
    e->o["connected"] = J::boolean(p->conn != nullptr);
    led->a.push_back(e);
  }
  return led;
}

J::Ptr Table::statusJson() const {
  auto j = J::obj();
  j->o["id"] = J::str(id);
  j->o["closed"] = J::boolean(closed);
  j->o["ledger"] = ledgerJson();
  return j;
}

void Table::sendState() {
  for (auto& p : players) {
    if (p->conn) net->sendText(p->conn, J::dump(stateJson(p->id)));
  }
}

void Table::handleMsg(Conn* c, int playerId, const J::Ptr& m) {
  Player* p = getP(playerId);
  if (!p) return;
  if (p->conn != c) return;
  std::string t = J::gs(m, "t");
  if (t == "join") {
    std::string name = J::gs(m, "name");
    int stack = J::gi(m, "stack");
    if (name.empty()) { errTo(p, "name required"); return; }
    if (stack <= 0) stack = st.bb * 100;
    p->name = name;
    settleHostPresence();
    if (p->seated) { sendState(); return; }
    for (auto it = pending.begin(); it != pending.end();) {
      if (it->playerId == p->id) it = pending.erase(it);
      else ++it;
    }
    if (p->host) {
      seat(p, stack);
      sysMsg(p->name + " created the table");
    } else {
      Pending r;
      r.id = nextPendId++;
      r.playerId = p->id;
      r.name = name;
      r.stack = stack;
      r.type = 0;
      int pref = J::gi(m, "seat");
      if (pref < 0 || pref > 9) pref = -1;
      r.seat = pref;
      pending.push_back(r);
    }
    sendState();
    return;
  }
  if (t == "watch") {
    std::string name = J::gs(m, "name");
    if (name.empty()) name = "guest" + std::to_string(p->id);
    p->name = name;
    settleHostPresence();
    if (p->host && !p->seated) seat(p, st.bb * 100);
    sendState();
    return;
  }
  if (t == "move") {
    if (!p->seated) { errTo(p, "not seated"); return; }
    int seat = J::gi(m, "seat");
    if (seat < 0 || seat > 9) { errTo(p, "bad seat"); return; }
    if (p->inHand) { errTo(p, "cannot move during a hand"); return; }
    if (p->seat == seat) { sendState(); return; }
    if (bySeat(seat)) { errTo(p, "seat is occupied"); return; }
    p->seat = seat;
    sysMsg(p->name + " moved to seat " + std::to_string(seat + 1));
    sendState();
    return;
  }
  if (t == "chat") {
    std::string msg = J::gs(m, "m");
    if (msg.empty()) return;
    if (msg.size() > 500) msg = msg.substr(0, 500);
    auto j = J::obj();
    j->o["t"] = J::str("chat");
    j->o["id"] = J::number(p->id);
    j->o["name"] = J::str(p->name);
    j->o["m"] = J::str(msg);
    auto keep = J::obj();
    keep->o["name"] = J::str(p->name);
    keep->o["m"] = J::str(msg);
    chatRec.push_back(keep);
    while (chatRec.size() > 50) chatRec.pop_front();
    broadcast(J::dump(j));
    return;
  }
  if (t == "act") {
    applyAction(p, J::gs(m, "a"), J::gi(m, "amt"));
    return;
  }
  if (t == "discard") {
    std::vector<int> idx;
    auto it = m->o.find("idx");
    if (it != m->o.end() && it->second && it->second->t == J::V::ARR) {
      for (auto& x : it->second->a) if (x && x->t == J::V::NUM) idx.push_back((int)x->num);
    }
    applyDiscard(p, idx);
    return;
  }
  if (t == "run2") {
    if (phase == 3 && p->r2Asked && !p->r2Answered) {
      p->r2Answered = true;
      p->r2Ans = J::gb(m, "ans");
      resolveRun2();
    }
    return;
  }
  if (t == "rabbit") {
    if (handActive || !rabbitCards.empty()) return;
    int need = handSt.boardCards() - (int)board.size();
    if (need <= 0) return;
    int take = std::min(need, (int)deckCards.size());
    for (int i = 0; i < take; i++) rabbitCards.push_back(deckCards[deckCards.size() - 1 - i]);
    sendState();
    return;
  }
  if (t == "sitout") {
    if (!p->seated) { errTo(p, "not seated"); return; }
    p->sittingOut = J::gb(m, "v");
    sysMsg(p->name + (p->sittingOut ? " is sitting out" : " is back in"));
    sendState();
    return;
  }
  if (t == "straddle") {
    if (phase == 6 && p->id == toActId) {
      applyStraddle(p, J::gb(m, "v"));
    }
    return;
  }
  if (t == "show" || t == "muck") {
    if (!p->showChoice) { errTo(p, "no pending choice"); return; }
    p->showChoice = false;
    if (t == "show") {
      p->show = true;
      if (lastHand) {
        auto pl = lastHand->o.find("players");
        if (pl != lastHand->o.end() && pl->second && pl->second->t == J::V::ARR) {
          for (auto& e : pl->second->a) {
            if (e && J::gi(e, "id") == p->id) {
              auto hh = J::arr();
              for (auto& c : p->hole) hh->a.push_back(J::str(c.str()));
              e->o["hole"] = hh;
            }
          }
        }
      }
      auto m2 = J::obj();
      m2->o["t"] = J::str("hand");
      for (auto& kv : lastHand->o) m2->o[kv.first] = kv.second;
      broadcast(J::dump(m2));
    }
    sendState();
    return;
  }
  if (t == "set") {
    if (!p->host) { errTo(p, "host only"); return; }
    Settings ns = st;
    if (J::has(m, "sb")) ns.sb = J::gi(m, "sb");
    if (J::has(m, "bb")) ns.bb = J::gi(m, "bb");
    if (J::has(m, "runTwice")) ns.runTwice = J::gb(m, "runTwice");
    if (J::has(m, "revealAllIn")) ns.revealAllIn = J::gb(m, "revealAllIn");
    if (J::has(m, "game27")) ns.game27 = J::gb(m, "game27");
    if (J::has(m, "value27")) ns.value27 = J::gi(m, "value27");
    if (J::has(m, "nitGame")) ns.nitGame = J::gb(m, "nitGame");
    if (J::has(m, "nitValue")) ns.nitValue = J::gi(m, "nitValue");
    if (J::has(m, "straddle")) ns.straddle = J::gb(m, "straddle");
    if (J::has(m, "sec")) ns.sec = J::gi(m, "sec");
    if (J::has(m, "paused")) ns.paused = J::gb(m, "paused");
    if (J::has(m, "game")) ns.game = J::gi(m, "game");
    if (J::has(m, "bet")) ns.bet = J::gi(m, "bet");
    if (J::has(m, "greek")) ns.greek = J::gb(m, "greek");
    if (J::has(m, "pineapple")) ns.pineapple = J::gb(m, "pineapple");
    if (J::has(m, "irish")) ns.irish = J::gb(m, "irish");
    if (J::has(m, "shortdeck")) ns.shortdeck = J::gb(m, "shortdeck");
    if (J::has(m, "cards")) ns.cards = J::gi(m, "cards");
    if (J::has(m, "hilo")) ns.hilo = J::gb(m, "hilo");
    if (J::has(m, "noRiver")) ns.noRiver = J::gb(m, "noRiver");
    if (J::has(m, "extraRiver")) ns.extraRiver = J::gb(m, "extraRiver");
    ns.sanitize();
    bool wasPaused = st.paused;
    Settings tmpNew = ns, tmpOld = st;
    tmpNew.paused = tmpOld.paused;
    bool variantChanged = J::dump(tmpNew.toJson()) != J::dump(tmpOld.toJson());
    if (handActive && variantChanged) {
      pendingSettings = ns;
      pendingSettingsValid = true;
      st.paused = ns.paused;
      if (!wasPaused && st.paused) sysMsg("game paused; current hand will finish");
      sysMsg("settings will apply from the next hand");
    } else {
      st = ns;
      pendingSettingsValid = false;
      if (!wasPaused && st.paused) sysMsg("game paused; current hand will finish");
    }
    sendState();
    if (!handActive) maybeDeal();
    return;
  }
  if (t == "approve" || t == "reject") {
    if (!p->host) { errTo(p, "host only"); return; }
    int rid = J::gi(m, "id");
    for (auto it = pending.begin(); it != pending.end(); ++it) {
      if (it->id != rid) continue;
      Pending r = *it;
      pending.erase(it);
      Player* target = getP(r.playerId);
      if (t == "approve" && target) {
        if (r.type == 1) {
          target->stack += r.stack;
          target->boughtIn += r.stack;
          sysMsg(p->name + " added " + chipsStr(r.stack) + " to " + target->name);
        } else {
          seat(target, r.stack, r.seat);
          sysMsg(target->name + " joined the table");
        }
      }
      break;
    }
    sendState();
    if (!handActive) maybeDeal();
    return;
  }
  if (t == "transfer") {
    if (!p->host) { errTo(p, "host only"); return; }
    Player* target = getP(J::gi(m, "id"));
    if (!target || target == p || !target->conn) { errTo(p, "target must be online"); return; }
    makeHost(target, p);
    return;
  }
  if (t == "setstack") {
    if (!p->host) { errTo(p, "host only"); return; }
    Player* target = getP(J::gi(m, "id"));
    int value = J::gi(m, "amt");
    if (!target || !target->seated || value < 0) { errTo(p, "bad request"); return; }
    if (target->inHand) { errTo(p, "cannot set stack during a hand"); return; }
    int delta = value - target->stack;
    target->stack = value;
    target->boughtIn = std::max(0, target->boughtIn + delta);
    sysMsg(p->name + " set " + target->name + "'s stack to " + chipsStr(value));
    sendState();
    return;
  }
  if (t == "chips") {
    if (!p->host) { errTo(p, "host only"); return; }
    Player* target = getP(J::gi(m, "id"));
    int amt = J::gi(m, "amt");
    if (!target || !target->seated || amt == 0) { errTo(p, "bad request"); return; }
    if (amt < 0) {
      if (target->inHand) { errTo(p, "cannot remove chips during a hand"); return; }
      if (target->stack + amt < 0) { errTo(p, "not enough chips"); return; }
      target->stack += amt;
      target->boughtIn = std::max(0, target->boughtIn + amt);
      sysMsg(p->name + " removed " + chipsStr(-amt) + " chips from " + target->name);
    } else {
      target->stack += amt;
      target->boughtIn += amt;
      sysMsg(p->name + " added " + chipsStr(amt) + " chips to " + target->name);
    }
    sendState();
    return;
  }
  if (t == "remove") {
    if (!p->host) { errTo(p, "host only"); return; }
    Player* target = getP(J::gi(m, "id"));
    if (!target || !target->seated || target->host) { errTo(p, "cannot remove"); return; }
    sysMsg(target->name + " was removed from the table");
    unseat(target);
    sendState();
    return;
  }
  if (t == "stand") {
    if (!p->seated) { errTo(p, "not seated"); return; }
    bool wasHost = p->host;
    unseat(p);
    if (wasHost) {
      Player* target = randomOnlinePlayer(p);
      if (target) makeHost(target, p);
    }
    sendState();
    return;
  }
  if (t == "rebuy") {
    if (!p->seated) { errTo(p, "not seated"); return; }
    int amt = J::gi(m, "amt");
    if (amt <= 0) { errTo(p, "amount required"); return; }
    for (auto& r : pending) if (r.playerId == p->id && r.type == 1) { errTo(p, "rebuy already requested"); return; }
    Pending r;
    r.id = nextPendId++;
    r.playerId = p->id;
    r.name = p->name;
    r.stack = amt;
    r.type = 1;
    pending.push_back(r);
    sendState();
    return;
  }
  if (t == "hist") {
    auto j = J::obj();
    j->o["t"] = J::str("history");
    auto hands = J::arr();
    for (auto& h : history) hands->a.push_back(h);
    j->o["hands"] = hands;
    sendTo(p, J::dump(j));
    return;
  }
}
