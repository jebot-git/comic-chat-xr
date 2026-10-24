// SPDX-License-Identifier: MIT
#pragma once
#include "comic.h"
#include <algorithm>
namespace comic {
inline std::string
automaticBackdrop(const std::string &channel,
                  const std::vector<std::string> &available) {
  if (available.empty())
    return "room";
  if (channel.empty())
    return std::find(available.begin(), available.end(), "room") !=
                   available.end()
               ? "room"
               : available.front();
  auto name = casefold(channel);
  if (!name.empty() && (name[0] == '#' || name[0] == '&'))
    name.erase(0, 1);
  if (std::find(available.begin(), available.end(), name) != available.end())
    return name;
  uint32_t hash = 2166136261u;
  for (unsigned char c : casefold(channel))
    hash = (hash ^ c) * 16777619u;
  return available[hash % available.size()];
}
inline std::string messagePayload(const Message &message, const Pose &pose,
                                  bool sendComicFormatting) {
  if (sendComicFormatting)
    return annotate(message, pose);
  if (message.expression.mode == 5)
    return "\1"
           "ACTION " +
           message.text + "\1";
  if (message.expression.mode == 3)
    return "\1"
           "ACTION thinks " +
           message.text + "\1";
  return message.text;
}
} // namespace comic
