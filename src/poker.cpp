#include "poker.h"

static inline uint64_t K(int cat, int a, int b, int c, int d, int e) {
  return ((uint64_t)cat << 20) | ((uint64_t)a << 16) | ((uint64_t)b << 12) | ((uint64_t)c << 8) | ((uint64_t)d << 4) | (uint64_t)e;
}

static int straightHigh(int bits, bool sd) {
  if (sd) {
    int special = (0x0F << 4) | (1 << 12);
    if ((bits & special) == special) return 9;
    for (int hi = 14; hi >= 6; hi--) {
      if (((bits >> (hi - 6)) & 0x1F) == 0x1F) return hi;
    }
    return 0;
  }
  for (int hi = 14; hi >= 6; hi--) {
    if (((bits >> (hi - 6)) & 0x1F) == 0x1F) return hi;
  }
  if (((bits & 0xF) == 0xF) && (bits & (1 << 12))) return 5;
  return 0;
}

uint64_t eval5(const Card* cs, bool sd) {
  int cnt[15] = {0};
  bool flush = true;
  int bits = 0;
  for (int i = 0; i < 5; i++) {
    cnt[cs[i].r]++;
    bits |= 1 << (cs[i].r - 2);
    if (i && cs[i].s != cs[0].s) flush = false;
  }
  std::vector<int> q, t, p, s;
  for (int r = 14; r >= 2; r--) {
    if (cnt[r] == 4) q.push_back(r);
    else if (cnt[r] == 3) t.push_back(r);
    else if (cnt[r] == 2) p.push_back(r);
    else if (cnt[r] == 1) s.push_back(r);
  }
  int sh = straightHigh(bits, sd);
  if (flush && sh) return K(8, sh, 0, 0, 0, 0);
  if (!q.empty()) return K(7, q[0], s[0], 0, 0, 0);
  if (t.size() == 2) return K(sd ? 5 : 6, t[0], t[1], 0, 0, 0);
  if (t.size() == 1 && !p.empty()) return K(sd ? 5 : 6, t[0], p[0], 0, 0, 0);
  if (flush) return K(sd ? 6 : 5, s[0], s[1], s[2], s[3], s[4]);
  if (sh) return K(4, sh, 0, 0, 0, 0);
  if (t.size() == 1) return K(3, t[0], s[0], s[1], 0, 0);
  if (p.size() == 2) return K(2, p[0], p[1], s[0], 0, 0);
  if (p.size() == 1) return K(1, p[0], s[0], s[1], s[2], 0);
  return K(0, s[0], s[1], s[2], s[3], s[4]);
}

uint64_t best5(const std::vector<Card>& cards, bool sd) {
  Card c[5];
  uint64_t best = 0;
  int n = (int)cards.size();
  for (int a = 0; a < n; a++)
    for (int b = a + 1; b < n; b++)
      for (int c2 = b + 1; c2 < n; c2++)
        for (int d = c2 + 1; d < n; d++)
          for (int e2 = d + 1; e2 < n; e2++) {
            c[0] = cards[a]; c[1] = cards[b]; c[2] = cards[c2]; c[3] = cards[d]; c[4] = cards[e2];
            uint64_t k = eval5(c, sd);
            if (k > best) best = k;
          }
  return best;
}

uint64_t bestHoldem(const std::vector<Card>& hole, const std::vector<Card>& board, bool useBoth, bool sd) {
  if (useBoth) {
    Card c[5];
    c[0] = hole[0]; c[1] = hole[1];
    uint64_t best = 0;
    size_t m = board.size();
    for (size_t a = 0; a < m; a++)
      for (size_t b = a + 1; b < m; b++)
        for (size_t c2 = b + 1; c2 < m; c2++) {
          c[2] = board[a]; c[3] = board[b]; c[4] = board[c2];
          uint64_t k = eval5(c, sd);
          if (k > best) best = k;
        }
    return best;
  }
  std::vector<Card> all = hole;
  all.insert(all.end(), board.begin(), board.end());
  return best5(all, sd);
}

uint64_t bestOmaha(const std::vector<Card>& hole, const std::vector<Card>& board, bool sd) {
  Card c[5];
  uint64_t best = 0;
  size_t n = hole.size(), m = board.size();
  for (size_t i = 0; i < n; i++)
    for (size_t j = i + 1; j < n; j++)
      for (size_t a = 0; a < m; a++)
        for (size_t b = a + 1; b < m; b++)
          for (size_t c2 = b + 1; c2 < m; c2++) {
            c[0] = hole[i]; c[1] = hole[j]; c[2] = board[a]; c[3] = board[b]; c[4] = board[c2];
            uint64_t k = eval5(c, sd);
            if (k > best) best = k;
          }
  return best;
}

static inline int lowR(int r) { return r == 14 ? 1 : r; }

bool bestLowOmaha(const std::vector<Card>& hole, const std::vector<Card>& board, uint64_t& key) {
  bool found = false;
  key = UINT64_MAX;
  size_t n = hole.size(), m = board.size();
  for (size_t i = 0; i < n; i++)
    for (size_t j = i + 1; j < n; j++)
      for (size_t a = 0; a < m; a++)
        for (size_t b = a + 1; b < m; b++)
          for (size_t c2 = b + 1; c2 < m; c2++) {
            int r[5] = {lowR(hole[i].r), lowR(hole[j].r), lowR(board[a].r), lowR(board[b].r), lowR(board[c2].r)};
            bool ok = true;
            for (int x : r) if (x < 1 || x > 8) ok = false;
            std::sort(r, r + 5, std::greater<int>());
            for (int x = 0; x < 4; x++) if (r[x] == r[x + 1]) ok = false;
            if (!ok) continue;
            uint64_t k = 0;
            for (int x = 0; x < 5; x++) k = (k << 4) | (uint64_t)r[x];
            if (k < key) { key = k; found = true; }
          }
  return found;
}
