#pragma once
#include <string>
#include <map>
#include <memory>
#include <functional>

struct Conn {
  int fd = -1;
  std::string in;
  std::string out;
  bool http = true;
  bool ws = false;
  bool dead = false;
  bool closeAfter = false;
  std::string method, path, query;
  std::map<std::string, std::string> headers;
  std::string frag;
  void* user = nullptr;
  std::string tableId;
  bool isHostConn = false;
  int playerId = 0;
};

using HttpHandler = std::function<bool(Conn&)>;
using MsgHandler = std::function<void(Conn&, const std::string&)>;
using CloseHandler = std::function<void(Conn&)>;
using TickHandler = std::function<void()>;

class Net {
public:
  bool start(int port);
  void setHttp(HttpHandler h) { httpH = h; }
  void setMsg(MsgHandler h) { msgH = h; }
  void setClose(CloseHandler h) { closeH = h; }
  void setTick(TickHandler h) { tickH = h; }
  void run();
  void sendText(Conn* c, const std::string& s);
  void closeConn(Conn* c);
  void respond(Conn* c, int code, const std::string& type, const std::string& body);

private:
  int efd = -1, lfd = -1;
  HttpHandler httpH;
  MsgHandler msgH;
  CloseHandler closeH;
  TickHandler tickH;
  std::map<int, std::unique_ptr<Conn>> conns;
  void queue(Conn* c, const std::string& s);
  void flush(Conn* c);
  void handleRead(Conn* c);
  void acceptAll();
  void parseReq(Conn* c, const std::string& req);
  bool parseFrames(Conn* c);
  void wsHandshake(Conn* c);
};
