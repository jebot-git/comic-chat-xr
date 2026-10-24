// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
namespace comic {
struct InputCommand {
  std::string name, args;
};
inline InputCommand inputCommand(const std::string &text) {
  if (text.empty() || text[0] != '/' || text.rfind("//", 0) == 0)
    return {"say", text.rfind("//", 0) == 0 ? text.substr(1) : text};
  auto end = text.find_first_of(" \t");
  std::string name = text.substr(1, end == std::string::npos ? end : end - 1);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  auto start =
      end == std::string::npos ? end : text.find_first_not_of(" \t", end);
  return {name, start == std::string::npos ? "" : text.substr(start)};
}
inline bool ircToken(const std::string &s) {
  return !s.empty() && std::none_of(s.begin(), s.end(), [](unsigned char c) {
    return c <= 32 || c == 127 || c == ',' || c == ':';
  });
}
// Commands with IRC's ordinary parameter syntax. Free-form trailing parameters
// are handled explicitly by the UI, so users never need to supply a colon.
inline std::string protocolCommand(std::string name) {
  if (name == "quote")
    return "RAW";
  if (name == "leave")
    return "PART";
  if (name == "j")
    return "JOIN";
  if (name == "w")
    return "WHOIS";
  if (name == "action")
    return "ME";
  for (auto supported :
       {"join", "part", "nick", "topic", "mode", "names", "list", "who",
        "whois", "whowas", "invite", "kick", "away", "raw", "notice", "msg",
        "query", "me", "think"})
    if (name == supported) {
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::toupper(c); });
      return name;
    }
  return {};
}
} // namespace comic
