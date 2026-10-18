#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace J {
struct V;
using Ptr = std::shared_ptr<V>;
using Arr = std::vector<Ptr>;
using Obj = std::map<std::string, Ptr>;

struct V {
  enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
  bool b = false;
  double num = 0;
  std::string s;
  Arr a;
  Obj o;
  static Ptr mk() { return std::make_shared<V>(); }
  static Ptr nul() { auto v = mk(); v->t = NUL; return v; }
  static Ptr boolean(bool x) { auto v = mk(); v->t = BOOL; v->b = x; return v; }
  static Ptr number(double x) { auto v = mk(); v->t = NUM; v->num = x; return v; }
  static Ptr str(const std::string& x) { auto v = mk(); v->t = STR; v->s = x; return v; }
  static Ptr arr() { auto v = mk(); v->t = ARR; return v; }
  static Ptr obj() { auto v = mk(); v->t = OBJ; return v; }
  bool B() const { return b; }
  int I() const { return (int)num; }
};

inline int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

struct Parser {
  const char* p;
  const char* e;
  explicit Parser(const std::string& s) : p(s.c_str()), e(s.c_str() + s.size()) {}
  void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
  Ptr parse() { ws(); if (p >= e) return nullptr; auto v = value(); if (!v) return nullptr; ws(); return v; }
  Ptr value() {
    if (p >= e) return nullptr;
    char c = *p;
    if (c == '{') return object();
    if (c == '[') return array();
    if (c == '"') return V::str(str());
    if (c == 't') { if (e - p >= 4 && !strncmp(p, "true", 4)) { p += 4; return V::boolean(true); } return nullptr; }
    if (c == 'f') { if (e - p >= 5 && !strncmp(p, "false", 5)) { p += 5; return V::boolean(false); } return nullptr; }
    if (c == 'n') { if (e - p >= 4 && !strncmp(p, "null", 4)) { p += 4; return V::nul(); } return nullptr; }
    return number();
  }
  Ptr object() {
    p++;
    auto o = V::obj();
    ws();
    if (p < e && *p == '}') { p++; return o; }
    while (p < e) {
      ws();
      std::string k = str();
      ws();
      if (p >= e || *p != ':') return nullptr;
      p++;
      ws();
      auto v = value();
      if (!v) return nullptr;
      o->o[k] = v;
      ws();
      if (p < e && *p == ',') { p++; continue; }
      if (p < e && *p == '}') { p++; break; }
      return nullptr;
    }
    return o;
  }
  Ptr array() {
    p++;
    auto a = V::arr();
    ws();
    if (p < e && *p == ']') { p++; return a; }
    while (p < e) {
      ws();
      auto v = value();
      if (!v) return nullptr;
      a->a.push_back(v);
      ws();
      if (p < e && *p == ',') { p++; continue; }
      if (p < e && *p == ']') { p++; break; }
      return nullptr;
    }
    return a;
  }
  std::string str() {
    p++;
    std::string out;
    while (p < e) {
      char c = *p++;
      if (c == '"') return out;
      if (c == '\\') {
        if (p >= e) break;
        char d = *p++;
        switch (d) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (e - p >= 4) {
              unsigned v = 0;
              for (int i = 0; i < 4; i++) v = v * 16 + hexv(*p++);
              if (v < 0x80) out += (char)v;
              else if (v < 0x800) { out += (char)(0xC0 | (v >> 6)); out += (char)(0x80 | (v & 0x3F)); }
              else { out += (char)(0xE0 | (v >> 12)); out += (char)(0x80 | ((v >> 6) & 0x3F)); out += (char)(0x80 | (v & 0x3F)); }
            }
            break;
          }
          default: out += d;
        }
      } else out += c;
    }
    return out;
  }
  Ptr number() {
    const char* s = p;
    if (p < e && *p == '-') p++;
    while (p < e && *p >= '0' && *p <= '9') p++;
    if (p < e && *p == '.') { p++; while (p < e && *p >= '0' && *p <= '9') p++; }
    if (p < e && (*p == 'e' || *p == 'E')) {
      p++;
      if (p < e && (*p == '+' || *p == '-')) p++;
      while (p < e && *p >= '0' && *p <= '9') p++;
    }
    return V::number(strtod(s, nullptr));
  }
};

inline Ptr parse(const std::string& s) {
  if (s.empty()) return nullptr;
  Parser p(s);
  return p.parse();
}

inline Ptr obj() { return V::obj(); }
inline Ptr arr() { return V::arr(); }
inline Ptr nul() { return V::nul(); }
inline Ptr str(const std::string& s) { return V::str(s); }
inline Ptr number(double x) { return V::number(x); }
inline Ptr boolean(bool b) { return V::boolean(b); }

inline void esc(const std::string& s, std::string& out) {
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((unsigned char)c < 0x20) out += " ";
        else out += c;
    }
  }
}

inline void write(const Ptr& v, std::string& out) {
  if (!v) { out += "null"; return; }
  switch (v->t) {
    case V::NUL: out += "null"; break;
    case V::BOOL: out += v->b ? "true" : "false"; break;
    case V::NUM: {
      char buf[48];
      if (v->num == (int64_t)v->num) snprintf(buf, sizeof buf, "%lld", (long long)v->num);
      else snprintf(buf, sizeof buf, "%g", v->num);
      out += buf;
      break;
    }
    case V::STR: out += '"'; esc(v->s, out); out += '"'; break;
    case V::ARR: {
      out += '[';
      bool f = true;
      for (auto& x : v->a) { if (!f) out += ','; f = false; write(x, out); }
      out += ']';
      break;
    }
    case V::OBJ: {
      out += '{';
      bool f = true;
      for (auto& kv : v->o) {
        if (!f) out += ',';
        f = false;
        out += '"'; esc(kv.first, out); out += "\":";
        write(kv.second, out);
      }
      out += '}';
      break;
    }
  }
}

inline std::string dump(const Ptr& v) { std::string o; write(v, o); return o; }

inline std::string gs(const Ptr& m, const char* k) {
  if (!m) return "";
  auto it = m->o.find(k);
  if (it == m->o.end() || !it->second || it->second->t != V::STR) return "";
  return it->second->s;
}
inline int gi(const Ptr& m, const char* k) {
  if (!m) return 0;
  auto it = m->o.find(k);
  if (it == m->o.end() || !it->second || it->second->t != V::NUM) return 0;
  return (int)it->second->num;
}
inline bool gb(const Ptr& m, const char* k) {
  if (!m) return false;
  auto it = m->o.find(k);
  if (it == m->o.end() || !it->second) return false;
  return it->second->B();
}
inline bool has(const Ptr& m, const char* k) {
  if (!m) return false;
  return m->o.find(k) != m->o.end();
}
} // namespace J
