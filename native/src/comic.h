// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace comic {
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct Image {
  int w = 0, h = 0;
  std::vector<unsigned char> rgba;
  Image() = default;
  Image(int width, int height);
  static Image load(const fs::path &);
  void save(const fs::path &) const;
  void over(const Image &, int x, int y);
  Image flipped() const;
};
struct Expression {
  int emotion = 9, face = -1, torso = -1, gesture = 9, mode = 1;
  float intensity = 0, gestureIntensity = 0;
  bool requested = false, cooked = false;
};
struct Pose {
  std::shared_ptr<Image> image;
  int face = 0, torso = 0, headHeight = 0, faceX = 0;
  float faceEmotion = 9, faceIntensity = 0, bodyEmotion = 9, bodyIntensity = 0;
};
class Art {
  fs::path directory;
  Json data;
  std::map<std::string, std::shared_ptr<Image>> images;
  std::map<std::string, Pose> poses;

public:
  explicit Art(fs::path);
  std::vector<std::string> characters() const;
  std::vector<std::string> backdrops() const;
  const Json &catalog() const { return data; }
  std::shared_ptr<Image> image(const std::string &);
  std::shared_ptr<Image> backdrop(const std::string &);
  Pose pose(std::string character, const Expression &, bool flip = false);
};
struct Message {
  std::string nick, text, character;
  Expression expression;
  bool continuation = false;
};
struct Actor {
  std::string nick, character;
  Pose pose;
  float x = 0, y = 0, w = 0, h = 0;
  bool flip = false;
};
struct Panel {
  std::vector<Message> messages;
  std::string backdrop;
};
struct Layout {
  std::vector<Actor> actors;
};
struct Rendered {
  Image panel, balloons, tails;
  Layout layout;
};
class Document {
public:
  std::vector<Panel> panels;
  std::map<std::string, std::string> characters;
  std::string backdrop = "room", nick = "Guest", character = "anna",
              channel = "#comicchat";
  std::string font = "monospace";
  int fontSize = 18;
  void add(Message);
  void demo();
  Json json() const;
  void load(const Json &);
};
constexpr int PANEL = 480;
Layout layout(const Panel &, Art &);
struct BalloonMetrics {
  double width, height, lastLineWidth = 0;
};
BalloonMetrics measureBalloon(const std::string &, const std::string &font,
                              int fontSize);
bool balloonsFit(const Panel &, const std::string &font, int fontSize);
Rendered render(const Panel &, Art &, const std::string &font = "monospace",
                int fontSize = 18, int balloonRasterScale = 1);
void exportScene(const Panel &, Art &, const fs::path &,
                 const std::string &font = "monospace", int fontSize = 18);
void exportCharacter(const std::string &, const Expression &, Art &,
                     const fs::path &);
void exportAssets(Art &, const fs::path &);
Expression infer(const std::string &);
std::string annotate(const Message &, const Pose &);
std::string parseAnnotations(std::string, Expression &);
std::string casefold(std::string);
std::string plain(std::string);
struct IrcLine {
  std::string prefix, command;
  std::vector<std::string> args;
};
IrcLine parseIrc(const std::string &);
} // namespace comic
