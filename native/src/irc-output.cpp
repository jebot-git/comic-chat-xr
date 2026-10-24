// SPDX-License-Identifier: MIT
#include "irc-output.h"
#include <algorithm>
namespace comic {
std::string ircOutput(const IrcLine &line) {
  const auto &command = line.command;
  const auto &args = line.args;
  auto origin = line.prefix.substr(0, line.prefix.find('!'));
  auto join = [&](size_t first) {
    std::string out;
    for (size_t i = first; i < args.size(); ++i) {
      if (i > first)
        out += ' ';
      out += args[i];
    }
    return plain(out);
  };
  auto channel = [](const std::string &value) {
    return !value.empty() && (value.front() == '#' || value.front() == '&');
  };
  if (command.empty() || command == "PING" || command == "PONG")
    return {};
  if (command == "PRIVMSG" && args.size() >= 2) {
    Expression expression;
    auto body = parseAnnotations(args[1], expression);
    if (body.rfind("# Appears as ", 0) == 0)
      return {};
    auto context = "[" + args[0] + "] ";
    if (body.rfind("\1"
                   "ACTION ",
                   0) == 0) {
      body.erase(0, 8);
      if (!body.empty() && body.back() == '\1')
        body.pop_back();
      return plain(context + "* " + origin + " " + body);
    }
    if (!body.empty() && body.front() == '\1')
      return {};
    if (expression.mode == 5)
      return plain(context + "* " + origin + " " + body);
    return plain(context + origin + ": " + body);
  }
  if (command == "NOTICE" && args.size() >= 2)
    return plain("[" + (channel(args[0]) ? args[0] : "server") + "] -" +
                 origin + "- " + args[1]);
  if (command == "JOIN" && !args.empty())
    return plain("[" + args[0] + "] * " + origin + " joined");
  if (command == "PART" && !args.empty())
    return plain("[" + args[0] + "] * " + origin + " left" +
                 (args.size() > 1 ? " (" + join(1) + ")" : ""));
  if (command == "QUIT")
    return plain("* " + origin + " quit" +
                 (args.empty() ? "" : " (" + join(0) + ")"));
  if (command == "NICK" && !args.empty())
    return plain("* " + origin + " is now " + args[0]);
  if (command == "TOPIC" && args.size() >= 2)
    return plain("[" + args[0] + "] * " + origin + " set topic: " + args[1]);
  if (command == "KICK" && args.size() >= 2)
    return plain("[" + args[0] + "] * " + origin + " kicked " + args[1] +
                 (args.size() > 2 ? " (" + join(2) + ")" : ""));
  if (command == "MODE" && args.size() >= 2)
    return plain("[" + args[0] + "] * " + origin + " MODE " + join(1));
  bool numeric = command.size() == 3 &&
                 std::all_of(command.begin(), command.end(),
                             [](char c) { return c >= '0' && c <= '9'; });
  return plain("[server] " + command +
               (args.empty() ? "" : " " + join(numeric ? 1 : 0)));
}
} // namespace comic
