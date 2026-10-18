#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <functional>
#include "json.h"
#include "poker.h"
#include "net.h"

extern bool gVerbose;

struct Settings {
  int sb = 100, bb = 200;
  bool runTwice = false;
  bool revealAllIn = false;
  int sec = 20;
  bool paused = true;
  int game = 0;
  int bet = 0;
  bool greek = false, pineapple = false, irish = false, shortdeck = false;
  int cards = 2;
  bool hilo = false;
  bool noRiver = false, extraRiver = false;
  bool game27 = false;
  int value27 = 100;
  bool nitGame = false;
  int nitValue = 100;
  bool straddle = false;
  void sanitize();
  int deckSize() const { return shortdeck ? 36 : 52; }
  int boardCards() const { return noRiver ? 4 : (extraRiver ? 6 : 5); }
  int maxPlayers() const;
  std::string variant() const;
  J::Ptr toJson() const;
};

struct Player {
  int id = 0;
  std::string name;
  Conn* conn = nullptr;
  bool host = false;
  bool seated = false;
  int seat = -1;
  int stack = 0;
  int boughtIn = 0;
  std::vector<Card> hole;
  bool inHand = false, folded = false, allIn = false, acted = false, show = false;
  int streetBet = 0, totalComm = 0, stackStart = 0;
  int bank = 60;
  bool r2Asked = false, r2Answered = false, r2Ans = false;
  bool showChoice = false;
  bool sittingOut = false;
  int wonHands = 0;
};

struct Pending {
  int id = 0;
  int playerId = 0;
  std::string name;
  int stack = 0;
  int type = 0;
  int seat = -1;
};

class Table {
public:
  Table(const std::string& tableId, const std::string& hostKey, Net* net);
  Player* addPlayer(Conn* c);
  Player* reattachHost(Conn* c);
  void onDisconnect(Conn* c, int playerId);
  void handleMsg(Conn* c, int playerId, const J::Ptr& m);
  void tick(int64_t now);

  std::string id;
  std::string hostKey;
  Settings st;
  bool pendingSettingsValid = false;
  Settings pendingSettings;

private:
  Net* net;
  std::vector<std::unique_ptr<Player>> players;
  std::vector<Pending> pending;
  int nextPendId = 1;
  int handCount = 0;
  bool handActive = false;
  std::mt19937_64 rng;

  Settings handSt;
  int hId = 0, dealerSeat = -1, sbSeat = -1, bbSeat = -1;
  int street = 0, currentBet = 0, lastRaise = 0;
  int toActId = 0;
  int phase = 0;
  std::vector<Card> board, boardB;
  bool run2 = false, pendingRun2 = false, run2Offered = false;
  int64_t turnDeadline = 0, r2Deadline = 0, turnWindow = 0;
  int64_t revealAt = 0, nextDealAt = 0;
  int turnPhase = 0;
  int postedSB = 0, postedBB = 0;
  J::Ptr lastHand;
  std::map<int, double> equities;
  std::vector<J::Ptr> handLog;
  std::vector<Card> rabbitCards;
  int straddleSeat = -1, straddlePlayerId = 0;
  bool straddleActive = false;
  int postedStraddle = 0;
  int nitId = 0;
  std::vector<Player*> dq;
  int dqIdx = 0;
  std::vector<Card> deckCards;
  std::deque<J::Ptr> history;
  std::deque<J::Ptr> chatRec;

  Player* getP(int id);
  Player* bySeat(int s);
  int seatedCount() const;
  std::vector<Player*> seatsInOrder(int fromSeat, const std::function<bool(Player*)>& pred);
  std::vector<Player*> inHandOrder();
  std::vector<Player*> nonFolded();
  std::vector<Player*> active();
  bool hasMoreStreets() const;
  int totalPot() const;
  int displayPot() const;

  void sendState();
  J::Ptr stateJson(int forId);
  void broadcast(const std::string& s);
  void sendTo(Player* p, const std::string& s);
  void sysMsg(const std::string& m);
  void errTo(Player* p, const std::string& m);

  void seat(Player* p, int stack, int prefSeat = -1);
  void unseat(Player* p);

  void maybeDeal();
  void dealHand();
  void startHandDeal();
  void applyStraddle(Player* p, bool yes);
  bool is27off(const std::vector<Card>& h);
  void applySideGames(std::map<int, int>& win);
  void postBlinds();
  void beginStreet();
  void nextToAct();
  bool needsAction(Player* p) const;
  void applyAction(Player* p, const std::string& a, int amt);
  int minRaiseTo(Player* p) const;
  int maxRaiseTo(Player* p) const;
  void doFold(Player* p);
  void endStreet();
  void finalizeRun2OrDeal();
  bool allInPacing();
  void dealNextStreet();
  void dealBoardCard();
  void startDiscard();
  void applyDiscard(Player* p, const std::vector<int>& idx);
  void autoDiscard(Player* p);
  void offerRun2();
  void resolveRun2();
  void showdown();
  void awardTier(int amt, const std::vector<Player*>& elig, std::map<int, int>& win);
  void awardTierSingle(int amt, const std::vector<Player*>& elig, const std::vector<Card>& b, std::map<int, int>& win);
  void split(const std::vector<Player*>& w, int amt, std::map<int, int>& win);
  uint64_t bestFor(Player* p, const std::vector<Card>& b);
  bool lowFor(Player* p, const std::vector<Card>& b, uint64_t& k);
  void endHand();
  void autoAct(Player* p);
  void logAct(int street, Player* p, const std::string& a, int amt);
  void computeEquity();
  void sfx(const std::string& s, int n, Player* to);
};
