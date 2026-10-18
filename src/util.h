#pragma once
#include <cstdint>
#include <string>
#include <random>
#include <chrono>
#include <cstring>
#include <cstdio>

inline int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

struct Rng {
  std::mt19937_64 g;
  Rng() { std::random_device rd; g.seed(rd() ^ (uint64_t)nowMs()); }
  uint64_t next() { return g(); }
};

inline Rng& globalRng() { static Rng r; return r; }

inline std::string randHex(int n) {
  static const char* h = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < n; i++) s += h[globalRng().next() % 16];
  return s;
}

inline std::string sha1Raw(const std::string& data) {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  uint64_t bitlen = (uint64_t)data.size() * 8;
  std::string msg = data;
  msg += (char)0x80;
  while (msg.size() % 64 != 56) msg += (char)0;
  for (int i = 7; i >= 0; i--) msg += (char)((bitlen >> (i * 8)) & 0xFF);
  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint8_t)msg[off + i * 4] << 24) | ((uint8_t)msg[off + i * 4 + 1] << 16) |
             ((uint8_t)msg[off + i * 4 + 2] << 8) | ((uint8_t)msg[off + i * 4 + 3]);
    for (int i = 16; i < 80; i++) {
      uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
      w[i] = (x << 1) | (x >> 31);
    }
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t tmp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
      e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = tmp;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
  }
  std::string out;
  out.reserve(20);
  uint32_t hs[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; i++) for (int j = 3; j >= 0; j--) out += (char)((hs[i] >> (j * 8)) & 0xFF);
  return out;
}

inline std::string sha1Hex(const std::string& data) {
  std::string r = sha1Raw(data);
  char buf[41];
  for (int i = 0; i < 20; i++) snprintf(buf + i * 2, 3, "%02x", (unsigned char)r[i]);
  return std::string(buf, 40);
}

inline std::string b64Encode(const std::string& in) {
  static const char* B = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < in.size(); i += 3) {
    uint32_t v = (uint8_t)in[i] << 16;
    if (i + 1 < in.size()) v |= (uint8_t)in[i + 1] << 8;
    if (i + 2 < in.size()) v |= (uint8_t)in[i + 2];
    out += B[(v >> 18) & 63];
    out += B[(v >> 12) & 63];
    out += i + 1 < in.size() ? B[(v >> 6) & 63] : '=';
    out += i + 2 < in.size() ? B[v & 63] : '=';
  }
  return out;
}
