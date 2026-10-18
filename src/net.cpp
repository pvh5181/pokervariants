#include "net.h"
#include "util.h"
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <cstring>

static void setNonBlock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

bool Net::start(int port) {
  lfd = socket(AF_INET, SOCK_STREAM, 0);
  if (lfd < 0) return false;
  int one = 1;
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons((uint16_t)port);
  if (bind(lfd, (sockaddr*)&a, sizeof a) < 0) return false;
  if (listen(lfd, 128) < 0) return false;
  setNonBlock(lfd);
  efd = epoll_create1(0);
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = lfd;
  epoll_ctl(efd, EPOLL_CTL_ADD, lfd, &ev);
  return true;
}

void Net::acceptAll() {
  while (true) {
    sockaddr_in a{};
    socklen_t al = sizeof a;
    int fd = accept4(lfd, (sockaddr*)&a, &al, SOCK_NONBLOCK);
    if (fd < 0) return;
    auto c = std::make_unique<Conn>();
    c->fd = fd;
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
    conns[fd] = std::move(c);
  }
}

void Net::queue(Conn* c, const std::string& s) {
  if (!c || c->dead) return;
  bool wasEmpty = c->out.empty();
  c->out += s;
  if (c->out.size() > 8u * 1024 * 1024) { closeConn(c); return; }
  if (wasEmpty) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = c->fd;
    epoll_ctl(efd, EPOLL_CTL_MOD, c->fd, &ev);
  }
}

void Net::flush(Conn* c) {
  while (!c->out.empty()) {
    ssize_t n = send(c->fd, c->out.data(), c->out.size(), MSG_NOSIGNAL);
    if (n > 0) c->out.erase(0, (size_t)n);
    else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    else { closeConn(c); return; }
  }
  if (c->out.empty()) {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = c->fd;
    epoll_ctl(efd, EPOLL_CTL_MOD, c->fd, &ev);
    if (c->closeAfter) {
      c->closeAfter = false;
      closeConn(c);
    }
  }
}

void Net::respond(Conn* c, int code, const std::string& type, const std::string& body) {
  if (!c || c->dead) return;
  std::string h = "HTTP/1.1 " + std::to_string(code) + " X\r\nContent-Type: " + type +
                  "\r\nContent-Length: " + std::to_string(body.size()) +
                  "\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\n\r\n";
  c->out += h + body;
  c->closeAfter = true;
  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLOUT;
  ev.data.fd = c->fd;
  epoll_ctl(efd, EPOLL_CTL_MOD, c->fd, &ev);
}

void Net::closeConn(Conn* c) {
  if (!c || c->dead) return;
  c->dead = true;
  if (c->ws && !c->http) {
    char b[2] = {(char)0x88, 0};
    send(c->fd, b, 2, MSG_NOSIGNAL);
  }
  epoll_ctl(efd, EPOLL_CTL_DEL, c->fd, nullptr);
  ::close(c->fd);
  c->fd = -1;
  if (closeH) closeH(*c);
}

void Net::sendText(Conn* c, const std::string& s) {
  if (!c || c->dead || c->http) return;
  std::string f;
  f += (char)0x81;
  size_t n = s.size();
  if (n < 126) f += (char)n;
  else if (n < 65536) { f += (char)126; f += (char)(n >> 8); f += (char)(n & 0xFF); }
  else {
    f += (char)127;
    for (int i = 7; i >= 0; i--) f += (char)((n >> (i * 8)) & 0xFF);
  }
  f += s;
  queue(c, f);
}

void Net::parseReq(Conn* c, const std::string& req) {
  size_t nl = req.find("\r\n");
  if (nl == std::string::npos) return;
  std::string line = req.substr(0, nl);
  size_t s1 = line.find(' ');
  size_t s2 = line.find(' ', s1 + 1);
  if (s1 == std::string::npos || s2 == std::string::npos) return;
  c->method = line.substr(0, s1);
  std::string p = line.substr(s1 + 1, s2 - s1 - 1);
  size_t q = p.find('?');
  if (q == std::string::npos) c->path = p;
  else { c->path = p.substr(0, q); c->query = p.substr(q + 1); }
  size_t i = nl + 2;
  while (i < req.size()) {
    size_t e = req.find("\r\n", i);
    if (e == std::string::npos || e == i) break;
    std::string h = req.substr(i, e - i);
    size_t cpos = h.find(':');
    if (cpos != std::string::npos) {
      std::string k = h.substr(0, cpos), v = h.substr(cpos + 1);
      while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
      for (auto& ch : k) ch = (char)tolower((unsigned char)ch);
      c->headers[k] = v;
    }
    i = e + 2;
  }
}

void Net::wsHandshake(Conn* c) {
  std::string key = c->headers["sec-websocket-key"];
  std::string acc = b64Encode(sha1Raw(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
  std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n";
  queue(c, resp);
}

bool Net::parseFrames(Conn* c) {
  while (true) {
    const std::string& b = c->in;
    if (b.size() < 2) return true;
    uint8_t b0 = (uint8_t)b[0], b1 = (uint8_t)b[1];
    bool fin = b0 & 0x80;
    int op = b0 & 0x0F;
    bool masked = b1 & 0x80;
    uint64_t len = b1 & 0x7F;
    size_t off = 2;
    if (len == 126) {
      if (b.size() < 4) return true;
      len = ((uint8_t)b[2] << 8) | (uint8_t)b[3];
      off = 4;
    } else if (len == 127) {
      if (b.size() < 10) return true;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | (uint8_t)b[2 + i];
      off = 10;
    }
    if (len > (1u << 20)) { closeConn(c); return false; }
    char mask[4] = {0, 0, 0, 0};
    if (masked) {
      if (b.size() < off + 4) return true;
      memcpy(mask, b.data() + off, 4);
      off += 4;
    }
    if (b.size() < off + len) return true;
    std::string payload = b.substr(off, (size_t)len);
    if (masked) for (size_t i = 0; i < payload.size(); i++) payload[i] ^= mask[i % 4];
    c->in.erase(0, off + (size_t)len);
    if (op == 8) { closeConn(c); return false; }
    if (op == 9) {
      std::string p2 = payload;
      p2.insert(p2.begin(), (char)0x8A);
      queue(c, p2);
      continue;
    }
    if (op == 10) continue;
    if (op == 1 || op == 0) {
      if (c->frag.empty() && fin) {
        if (msgH) msgH(*c, payload);
      } else {
        c->frag += payload;
        if (fin) {
          std::string m = c->frag;
          c->frag.clear();
          if (msgH) msgH(*c, m);
        }
      }
      continue;
    }
  }
}

void Net::handleRead(Conn* c) {
  char buf[8192];
  while (true) {
    ssize_t r = recv(c->fd, buf, sizeof buf, 0);
    if (r > 0) c->in.append(buf, (size_t)r);
    else if (r == 0) { closeConn(c); return; }
    else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      closeConn(c);
      return;
    }
  }
  if (c->in.size() > 512u * 1024) { closeConn(c); return; }
  if (c->http) {
    size_t pos = c->in.find("\r\n\r\n");
    if (pos == std::string::npos) {
      if (c->in.size() > 16384) respond(c, 431, "text/plain", "Request too large");
      return;
    }
    std::string req = c->in.substr(0, pos + 4);
    c->in.erase(0, pos + 4);
    parseReq(c, req);
    std::string up = c->headers["upgrade"];
    bool isUp = up == "websocket" && c->headers.count("sec-websocket-key");
    if (isUp) {
      if (httpH && httpH(*c)) {
        c->http = false;
        c->ws = true;
        wsHandshake(c);
      } else {
        respond(c, 404, "text/plain", "not found");
      }
      if (c->dead) return;
    } else {
      if (httpH) httpH(*c);
      if (c->dead) return;
    }
    if (!c->http) parseFrames(c);
  } else {
    parseFrames(c);
  }
}

void Net::run() {
  epoll_event evs[64];
  int64_t lastPing = nowMs();
  while (true) {
    int n = epoll_wait(efd, evs, 64, 50);
    for (int i = 0; i < n; i++) {
      int fd = evs[i].data.fd;
      if (fd == lfd) { acceptAll(); continue; }
      auto it = conns.find(fd);
      if (it == conns.end()) continue;
      Conn* c = it->second.get();
      if (c->dead) continue;
      if (evs[i].events & EPOLLIN) handleRead(c);
      if (c->dead) continue;
      if (evs[i].events & EPOLLOUT) flush(c);
    }
    if (nowMs() - lastPing > 30000) {
      lastPing = nowMs();
      for (auto& kv : conns) {
        Conn* c = kv.second.get();
        if (!c->dead && !c->http) queue(c, std::string("\x89\x00", 2));
      }
    }
    if (tickH) tickH();
  }
}
