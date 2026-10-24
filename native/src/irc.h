// SPDX-License-Identifier: MIT
#pragma once
#include "comic.h"
#include <memory>
namespace comic {
struct Connection {
  std::string host = "irc.libera.chat", nick = "Guest", channel = "#comicchat",
              password;
  unsigned port = 6697;
  bool tls = true;
};
class Irc {
  struct State;
  std::shared_ptr<State> state;

public:
  Irc();
  ~Irc();
  void connect(Connection);
  void disconnect();
  bool send(std::string);
  std::vector<Json> receive();
  bool connected() const;
};
} // namespace comic
