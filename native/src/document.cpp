// SPDX-License-Identifier: MIT
#include "comic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <glib.h>
#include <sstream>
#include <stdexcept>
namespace comic {
std::string casefold(std::string s) {
  for (char &c : s) {
    if (c >= 'A' && c <= 'Z')
      c += 32;
    else if (c == '[')
      c = '{';
    else if (c == ']')
      c = '}';
    else if (c == '\\')
      c = '|';
    else if (c == '^')
      c = '~';
  }
  return s;
}
std::string plain(std::string s) {
  char *v = g_utf8_make_valid(s.data(), s.size());
  std::string out;
  const char *p = v;
  while (*p) {
    gunichar c = g_utf8_get_char(p);
    auto next = g_utf8_next_char(p);
    if (c >= 32 && c != 127)
      out.append(p, next - p);
    else if (c == '\t')
      out += ' ';
    p = next;
  }
  g_free(v);
  return out;
}
Expression infer(const std::string &s) {
  Expression e;
  auto t = casefold(s);
  e.intensity = .7f;
  if (t.find(":d") != std::string::npos ||
      t.find("haha") != std::string::npos || t.find("lol") != std::string::npos)
    e.emotion = 8;
  else if (t.find(":)") != std::string::npos ||
           t.find(":-)") != std::string::npos)
    e.emotion = 1;
  else if (t.find(";)") != std::string::npos)
    e.emotion = 2;
  else if (t.find(">:(") != std::string::npos)
    e.emotion = 6;
  else if (t.find(":(") != std::string::npos)
    e.emotion = 5;
  else if (t.find("!!!") != std::string::npos)
    e.emotion = 7;
  else if (t.find(":o") != std::string::npos)
    e.emotion = 4;
  else
    e.intensity = 0;
  return e;
}
std::string parseAnnotations(std::string s, Expression &e) {
  if (s.rfind("(#", 0) != 0)
    return s;
  auto end = s.find(") ");
  if (end == std::string::npos || end > 256)
    return s;
  auto prefix = s.substr(2, end - 2);
  size_t p = 0;
  auto triple = [&](int &index, int &emotion, float &intensity) {
    if (p + 3 > prefix.size())
      throw std::runtime_error("Short annotation");
    int i = (unsigned char)prefix[p++] - '0',
        em = (unsigned char)prefix[p++] - '0',
        v = (unsigned char)prefix[p++] - '0';
    if (i < -1 || i > 207 || em < 0 || em > 17 || v < 0 || v > 10)
      throw std::runtime_error("Invalid annotation values");
    index = i;
    emotion = em;
    intensity = v / 10.f;
  };
  try {
    Expression parsed = e;
    if (p < prefix.size() && prefix[p] == 'G') {
      ++p;
      triple(parsed.torso, parsed.gesture, parsed.gestureIntensity);
    }
    if (p < prefix.size() && prefix[p] == 'E') {
      ++p;
      triple(parsed.face, parsed.emotion, parsed.intensity);
    }
    if (p < prefix.size() && prefix[p] == 'R') {
      ++p;
      parsed.requested = true;
    }
    if (p < prefix.size() && prefix[p] == 'M') {
      ++p;
      if (p >= prefix.size() || prefix[p] < '1' || prefix[p] > '5')
        return s;
      parsed.mode = prefix[p++] - '0';
    }
    if (p < prefix.size() && prefix[p] == 'T')
      p = prefix.size();
    if (p != prefix.size())
      return s;
    parsed.cooked = true;
    e = parsed;
    return s.substr(end + 2);
  } catch (const std::exception &) {
    return s;
  }
}
std::string annotate(const Message &m, const Pose &p) {
  auto byte = [](int n) { return char('0' + std::clamp(n, -1, 207)); };
  std::string out = "(#G";
  out += byte(p.torso);
  out += byte(int(p.bodyEmotion));
  out += byte(int(p.bodyIntensity * 10));
  out += 'E';
  out += byte(p.face);
  out += byte(int(p.faceEmotion));
  out += byte(int(p.faceIntensity * 10));
  out += "RM";
  out += byte(m.expression.mode);
  return out + ") " + m.text;
}
IrcLine parseIrc(const std::string &s) {
  IrcLine out;
  size_t p = 0;
  if (p < s.size() && s[p] == '@') {
    p = s.find(' ');
    if (p == std::string::npos)
      return {};
    ++p;
  }
  if (p < s.size() && s[p] == ':') {
    auto end = s.find(' ', p);
    if (end == std::string::npos)
      return {};
    out.prefix = s.substr(p + 1, end - p - 1);
    p = end + 1;
  }
  auto end = s.find(' ', p);
  out.command = s.substr(p, end == std::string::npos ? s.size() - p : end - p);
  p = end;
  while (p != std::string::npos && p < s.size()) {
    while (p < s.size() && s[p] == ' ')
      ++p;
    if (p == s.size())
      break;
    if (s[p] == ':') {
      out.args.push_back(s.substr(p + 1));
      break;
    }
    end = s.find(' ', p);
    out.args.push_back(
        s.substr(p, end == std::string::npos ? s.size() - p : end - p));
    p = end;
    if (out.args.size() > 15)
      return {};
  }
  return out;
}
void Document::add(Message m) {
  m.nick = plain(m.nick);
  m.text = plain(m.text);
  if (m.text.empty())
    return;
  if (m.text.size() > 8192)
    throw std::runtime_error("Chat message exceeds 8192 bytes");
  auto key = casefold(m.nick);
  if (m.character.empty())
    m.character = characters.count(key) ? characters[key] : "";
  if (m.character.empty()) {
    static const std::vector<std::string> names = {
        "anna", "armando", "susan", "tiki", "dan", "buck", "xeno", "margaret"};
    uint32_t hash = 2166136261;
    for (unsigned char c : key)
      hash = (hash ^ c) * 16777619u;
    m.character = names[hash % names.size()];
  }
  characters[key] = m.character;
  auto append = [&](Message part) {
    bool changed = false;
    if (!panels.empty())
      for (auto &previous : panels.back().messages)
        if (casefold(previous.nick) == key) {
          const auto &a = previous.expression, &b = part.expression;
          changed = previous.character != part.character || a.face != b.face ||
                    a.torso != b.torso || a.emotion != b.emotion ||
                    a.intensity != b.intensity || a.gesture != b.gesture ||
                    a.gestureIntensity != b.gestureIntensity;
        }
    Panel candidate = panels.empty() ? Panel{{}, backdrop} : panels.back();
    candidate.messages.push_back(part);
    if (panels.empty() || panels.back().backdrop != backdrop || changed ||
        !balloonsFit(candidate, font, fontSize))
      panels.push_back({{}, backdrop});
    panels.back().messages.push_back(std::move(part));
  };
  size_t at = 0;
  bool continued = m.continuation;
  while (at < m.text.size()) {
    std::vector<size_t> boundaries{0};
    const char *base = m.text.c_str() + at,
               *end = m.text.c_str() + m.text.size();
    for (const char *p = base; p < end;) {
      p = g_utf8_next_char(p);
      boundaries.push_back(p - base);
    }
    size_t low = 1, high = boundaries.size() - 1, best = 0;
    Message remaining = m;
    remaining.text = m.text.substr(at);
    if (balloonsFit(Panel{{remaining}, backdrop}, font, fontSize)) {
      best = high;
      low = high + 1;
    }
    while (low <= high) {
      size_t middle = (low + high) / 2;
      Message part = m;
      part.text = m.text.substr(at, boundaries[middle]);
      Panel test{{part}, backdrop};
      if (balloonsFit(test, font, fontSize)) {
        best = middle;
        low = middle + 1;
      } else
        high = middle - 1;
    }
    if (best == 0)
      throw std::runtime_error("Selected font cannot fit a comic balloon");
    size_t count = boundaries[best];
    if (at + count < m.text.size()) {
      auto space = m.text.rfind(' ', at + count - 1);
      if (space != std::string::npos && space >= at + count / 2)
        count = space - at + 1;
    }
    Message part = m;
    part.text = m.text.substr(at, count);
    part.continuation = continued;
    append(std::move(part));
    at += count;
    continued = true;
  }
  while (panels.size() > 200)
    panels.erase(panels.begin());
}

void Document::demo() {
  panels.clear();
  backdrop = "room";
  add({"Anna", "Welcome to Comic Chat. :)", "anna", infer(":)")});
  add({"Armando", "The same original artwork, now around the dome!", "armando",
       infer(":D")});
  add({"Anna", "Pick an expression on the wheel, then say something.", "anna",
       infer(";)")});
  add({"Armando", "Hello from XR!", "armando", infer(":)")});
}
Json Document::json() const {
  Json ps = Json::array();
  for (auto &p : panels) {
    Json ms = Json::array();
    for (auto &m : p.messages)
      ms.push_back({{"nick", m.nick},
                    {"text", m.text},
                    {"character", m.character},
                    {"emotion", m.expression.emotion},
                    {"intensity", m.expression.intensity},
                    {"face", m.expression.face},
                    {"torso", m.expression.torso},
                    {"gesture", m.expression.gesture},
                    {"gesture_intensity", m.expression.gestureIntensity},
                    {"cooked", m.expression.cooked},
                    {"mode", m.expression.mode},
                    {"continuation", m.continuation}});
    ps.push_back({{"backdrop", p.backdrop}, {"messages", ms}});
  }
  return {{"format", 1}, {"panels", ps}};
}
void Document::load(const Json &j) {
  if (!j.contains("panels") || !j["panels"].is_array() ||
      j["panels"].size() > 200)
    throw std::runtime_error("Invalid or oversized comic transcript");
  std::vector<Panel> loaded;
  std::map<std::string, std::string> identities;
  for (auto &p : j["panels"]) {
    Panel panel;
    panel.backdrop = p.value("backdrop", "room");
    if (p.at("messages").size() > 20)
      throw std::runtime_error("Too many actors in a panel");
    for (auto &m : p["messages"]) {
      Message message{m.value("nick", "Guest"),
                      m.value("text", ""),
                      m.value("character", "anna"),
                      {}};
      message.nick = plain(message.nick);
      message.continuation = m.value("continuation", false);
      message.text = plain(message.text);
      if (message.text.size() > 8192)
        throw std::runtime_error("Transcript message too long");
      auto &e = message.expression;
      e.emotion = std::clamp(m.value("emotion", 9), 0, 17);
      e.intensity = std::clamp(m.value("intensity", 0.f), 0.f, 1.f);
      e.face = m.value("face", -1);
      e.torso = m.value("torso", -1);
      e.gesture = std::clamp(m.value("gesture", 9), 0, 17);
      e.gestureIntensity =
          std::clamp(m.value("gesture_intensity", 0.f), 0.f, 1.f);
      e.cooked = m.value("cooked", false);
      e.mode = std::clamp(m.value("mode", 1), 1, 5);
      identities[casefold(message.nick)] = message.character;
      panel.messages.push_back(std::move(message));
    }
    loaded.push_back(std::move(panel));
  }
  // Publish only after the entire transcript validates; malformed later records
  // must never erase an already-open comic.
  Document staged = *this;
  staged.panels.clear();
  staged.characters = identities;
  for (auto &panel : loaded) {
    staged.backdrop = panel.backdrop;
    staged.panels.push_back({{}, panel.backdrop});
    for (auto &message : panel.messages)
      staged.add(std::move(message));

  }
  panels = std::move(staged.panels);
  characters = std::move(identities);
  if (!panels.empty())
    backdrop = panels.back().backdrop;
}
Layout layout(const Panel &panel, Art &art) {
  Layout out;
  for (auto &m : panel.messages) {
    auto found =
        std::find_if(out.actors.begin(), out.actors.end(), [&](auto &a) {
          return casefold(a.nick) == casefold(m.nick);
        });
    if (found != out.actors.end())
      continue;
    Actor actor;
    actor.nick = m.nick;
    actor.character = m.character;
    actor.flip = !out.actors.empty();
    actor.pose = art.pose(m.character, m.expression, actor.flip);
    out.actors.push_back(std::move(actor));
  }
  if (out.actors.empty())
    return out;
  // Port of CUnitPanel::LayoutAvatars: common normHeight=100, 1.9 body ratio,
  // proportional widths, head-safe close-up zoom and equal interstices.
  float bodyHeight = int(PANEL / 1.9), total = 0, maxHead = 0;
  for (auto &a : out.actors) {
    a.h = bodyHeight;
    a.w = std::round(a.h * a.pose.image->w / a.pose.image->h);
    maxHead = std::max(maxHead, a.h * a.pose.headHeight / a.pose.image->h);
    total += a.w;
  }
  float zoom =
      total > PANEL
          ? PANEL / total
          : std::min(PANEL / total, bodyHeight / std::max(1.f, maxHead * 1.2f));
  if (zoom > 1 && zoom < 1.1)
    zoom = 1;
  total = 0;
  for (auto &a : out.actors) {
    a.w = std::round(a.w * zoom);
    a.h = std::round(a.h * zoom);
    total += a.w;
  }
  float margin = std::max(0.f, (PANEL - total) / (out.actors.size() + 1));
  float x = margin;
  for (auto &a : out.actors) {
    a.x = x;
    a.y = PANEL - bodyHeight; // Original zoom crops the lower body, keeping the
                              // opening pose fixed.
    if (zoom <= 1)
      a.y = PANEL - a.h;
    a.w = std::max(1.f, a.w);
    a.h = std::max(1.f, a.h);
    x += a.w + margin;
  }
  return out;
}
} // namespace comic
