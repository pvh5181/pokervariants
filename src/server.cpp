#include "game.h"
#include "net.h"
#include "json.h"
#include "util.h"
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <csignal>
#include <cstring>

static std::map<std::string, std::unique_ptr<Table>> tables;
static Net net;

static std::map<std::string, std::string> parseQuery(const std::string& q) {
  std::map<std::string, std::string> out;
  size_t i = 0;
  while (i < q.size()) {
    size_t a = q.find('&', i);
    if (a == std::string::npos) a = q.size();
    std::string kv = q.substr(i, a - i);
    size_t e = kv.find('=');
    if (e != std::string::npos) out[kv.substr(0, e)] = kv.substr(e + 1);
    i = a + 1;
  }
  return out;
}

#include <unistd.h>

static std::string exeDir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  size_t s = p.rfind('/');
  if (s == std::string::npos) return "";
  return p.substr(0, s);
}

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static void serveFile(Conn& c, const std::string& path, const std::string& type) {
  std::string b = readFile(exeDir() + "/public" + path);
  if (b.empty()) b = readFile("public" + path);
  if (b.empty()) net.respond(&c, 404, "text/plain", "not found");
  else net.respond(&c, 200, type, b);
}

static bool onHttp(Conn& c) {
  const std::string& p = c.path;
  if (c.method != "GET") {
    net.respond(&c, 405, "text/plain", "method not allowed");
    return false;
  }
  if (p.rfind("/api/table/", 0) == 0) {
    std::string id = p.substr(11);
    auto it = tables.find(id);
    if (it == tables.end()) {
      net.respond(&c, 404, "text/plain", "no such table");
      return false;
    }
    net.respond(&c, 200, "application/json", J::dump(it->second->statusJson()));
    return false;
  }
  if (p == "/api/new") {
    std::string id = randHex(5);
    std::string key = randHex(16);
    tables[id] = std::make_unique<Table>(id, key, &net);
    auto j = J::obj();
    j->o["id"] = J::str(id);
    j->o["hostKey"] = J::str(key);
    net.respond(&c, 200, "application/json", J::dump(j));
    return false;
  }
  if (p.rfind("/ws/", 0) == 0) {
    std::string id = p.substr(4);
    auto it = tables.find(id);
    if (it == tables.end()) {
      net.respond(&c, 404, "text/plain", "no such table");
      return false;
    }
    if (it->second->closed) {
      net.respond(&c, 410, "text/plain", "table closed");
      return false;
    }
    auto q = parseQuery(c.query);
    c.user = it->second.get();
    c.tableId = id;
    c.isHostConn = q.count("k") && q["k"] == it->second->hostKey;
    return true;
  }
  if (p == "/" || p == "/index.html" || p.rfind("/t/", 0) == 0) {
    serveFile(c, "/index.html", "text/html; charset=utf-8");
    return false;
  }
  if (p == "/app.js") { serveFile(c, "/app.js", "text/javascript; charset=utf-8"); return false; }
  if (p == "/style.css") { serveFile(c, "/style.css", "text/css; charset=utf-8"); return false; }
  if (p.rfind("/sounds/", 0) == 0) { serveFile(c, p, "audio/mpeg"); return false; }
  net.respond(&c, 404, "text/plain", "not found");
  return false;
}

static void onMsg(Conn& c, const std::string& m) {
  Table* t = (Table*)c.user;
  if (!t) return;
  J::Ptr j = J::parse(m);
  if (!j) return;
  if (c.playerId == 0) {
    std::string t0 = J::gs(j, "t");
    if (t0 != "join" && t0 != "watch") {
      net.sendText(&c, R"({"t":"err","m":"first message must be join or watch"})");
      return;
    }
    std::string tok = J::gs(j, "tok");
    Player* p = t->reattachToken(&c, tok);
    if (!p && c.isHostConn) p = t->reattachHost(&c);
    if (!p) p = t->addPlayer(&c);
    c.playerId = p->id;
    auto w = J::obj();
    w->o["t"] = J::str("welcome");
    w->o["id"] = J::number(p->id);
    w->o["host"] = J::boolean(p->host);
    w->o["table"] = J::str(t->id);
    w->o["token"] = J::str(p->token);
    net.sendText(&c, J::dump(w));
  }
  t->handleMsg(&c, c.playerId, j);
}

static void onClose(Conn& c) {
  Table* t = (Table*)c.user;
  if (t && c.playerId) t->onDisconnect(&c, c.playerId);
}

static void onTick() {
  int64_t now = nowMs();
  for (auto it = tables.begin(); it != tables.end();) {
    it->second->tick(now);
    if (it->second->closed && now - it->second->closedAt > 24LL * 3600 * 1000) {
      it = tables.erase(it);
    } else {
      ++it;
    }
  }
}

int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  setvbuf(stdout, NULL, _IOLBF, 0);
  int port = 8080;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) gVerbose = true;
    else port = atoi(argv[i]);
  }
  const char* env = getenv("PORT");
  if (env && *env) port = atoi(env);
  const char* venv = getenv("VERBOSE");
  if (venv && *venv && *venv != '0') gVerbose = true;
  if (!net.start(port)) {
    fprintf(stderr, "failed to listen on port %d\n", port);
    return 1;
  }
  printf("PokerVariants server listening on port %d%s\n", port, gVerbose ? " (verbose)" : "");
  net.setHttp(onHttp);
  net.setMsg(onMsg);
  net.setClose(onClose);
  net.setTick(onTick);
  net.run();
  return 0;
}
