#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

struct Card {
  int r = 0;
  int s = 0;
  bool valid() const { return r >= 2 && r <= 14; }
  std::string str() const {
    std::string o;
    if (r <= 9) o += (char)('0' + r);
    else o += "TJQKA"[r - 10];
    o += "shdc"[s];
    return o;
  }
};

inline bool operator==(const Card& a, const Card& b) { return a.r == b.r && a.s == b.s; }

struct Deck {
  std::vector<Card> cs;
  int top = 0;
  void build(bool shortdeck) {
    cs.clear();
    for (int s = 0; s < 4; s++) {
      if (shortdeck) {
        int rs[] = {14, 6, 7, 8, 9, 10, 11, 12, 13};
        for (int r : rs) cs.push_back({r, s});
      } else {
        for (int r = 2; r <= 14; r++) cs.push_back({r, s});
      }
    }
    top = 0;
  }
  void shuffle(std::mt19937_64& g) {
    std::shuffle(cs.begin(), cs.end(), g);
    top = 0;
  }
  bool empty() const { return top >= (int)cs.size(); }
  int size() const { return (int)cs.size() - top; }
  Card pop() { return cs[top++]; }
};

uint64_t eval5(const Card* c, bool shortdeck);
uint64_t best5(const std::vector<Card>& cards, bool shortdeck);
uint64_t bestHoldem(const std::vector<Card>& hole, const std::vector<Card>& board, bool useBoth, bool shortdeck);
uint64_t bestOmaha(const std::vector<Card>& hole, const std::vector<Card>& board, bool shortdeck);
bool bestLowOmaha(const std::vector<Card>& hole, const std::vector<Card>& board, uint64_t& key);
