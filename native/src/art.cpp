// SPDX-License-Identifier: MIT
// AVB pose selection/alignment derived from Microsoft avatar.cpp/bodycam.cpp.
#include "comic.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <png.h>
#include <stdexcept>
namespace comic {
Image::Image(int width, int height)
    : w(width), h(height), rgba(size_t(width) * height * 4) {
  if (width < 1 || height < 1 || width > 8192 || height > 8192)
    throw std::runtime_error("Image dimensions exceed limit");
}
Image Image::load(const fs::path &path) {
  png_image src{};
  src.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&src, path.c_str()))
    throw std::runtime_error("Cannot read image: " + path.string());
  if (src.width < 1 || src.height < 1 || src.width > 4096 ||
      src.height > 4096) {
    png_image_free(&src);
    throw std::runtime_error("Oversized image");
  }
  src.format = PNG_FORMAT_RGBA;
  Image out(src.width, src.height);
  if (!png_image_finish_read(&src, nullptr, out.rgba.data(), 0, nullptr)) {
    std::string error = src.message;
    png_image_free(&src);
    throw std::runtime_error(error);
  }
  png_image_free(&src);
  return out;
}
void Image::save(const fs::path &path) const {
  png_image dst{};
  dst.version = PNG_IMAGE_VERSION;
  dst.width = w;
  dst.height = h;
  dst.format = PNG_FORMAT_RGBA;
  if (!png_image_write_to_file(&dst, path.c_str(), 0, rgba.data(), 0, nullptr))
    throw std::runtime_error("Cannot write image: " + path.string());
}
void Image::over(const Image &src, int x, int y) {
  for (int sy = 0; sy < src.h; ++sy)
    for (int sx = 0; sx < src.w; ++sx) {
      int dx = x + sx, dy = y + sy;
      if (dx < 0 || dy < 0 || dx >= w || dy >= h)
        continue;
      auto s = &src.rgba[(size_t(sy) * src.w + sx) * 4];
      auto d = &rgba[(size_t(dy) * w + dx) * 4];
      unsigned a = s[3], old = d[3], combined = a * 255 + old * (255 - a);
      for (int c = 0; c < 3; ++c)
        d[c] = combined ? (s[c] * a * 255 + d[c] * old * (255 - a)) / combined
                        : 255;
      d[3] = combined / 255;
    }
}
Image Image::flipped() const {
  Image out(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      std::copy_n(&rgba[(size_t(y) * w + x) * 4], 4,
                  &out.rgba[(size_t(y) * w + w - x - 1) * 4]);
  return out;
}
Art::Art(fs::path path) : directory(std::move(path)) {
  std::ifstream in(directory / "catalog.json");
  if (!in)
    throw std::runtime_error("Missing imported Comic Chat art catalog");
  in >> data;
}
std::vector<std::string> Art::characters() const {
  std::vector<std::string> out;
  for (auto it = data.at("characters").begin();
       it != data.at("characters").end(); ++it)
    out.push_back(it.key());
  return out;
}
std::vector<std::string> Art::backdrops() const {
  std::vector<std::string> out;
  for (auto it = data.at("backdrops").begin(); it != data.at("backdrops").end();
       ++it)
    out.push_back(it.key());
  return out;
}
std::shared_ptr<Image> Art::image(const std::string &name) {
  auto it = images.find(name);
  if (it != images.end())
    return it->second;
  auto out = std::make_shared<Image>(Image::load(directory / name));
  images[name] = out;
  return out;
}
std::shared_ptr<Image> Art::backdrop(const std::string &name) {
  if (!data["backdrops"].contains(name))
    throw std::runtime_error("Unknown backdrop: " + name);
  return image(data["backdrops"][name]["image"]);
}
static float angle(int emotion) {
  return emotion >= 10                  ? 1001.f + emotion - 10
         : emotion == 0 || emotion == 9 ? 0.f
                                        : (emotion - 1) * float(M_PI) / 4;
}
static int choose(const Json &records, int emotion, float intensity,
                  int previous = -1) {
  if (records.empty())
    return -1;
  float target = angle(emotion), bestAngle = 1e9f, bestIntensity = 1e9f;
  int best = 0;
  for (size_t j = 0; j < records.size(); ++j) {
    int i = (previous + 1 + j) % records.size();
    auto &r = records[i];
    int candidate = r["emotion"];
    float a = angle(candidate);
    float delta = target > 7 || a > 7
                      ? (target == a ? 0 : 10000)
                      : std::abs(std::remainder(a - target, 2 * float(M_PI)));
    float di = std::abs(r["intensity"].get<float>() - intensity);
    if (delta < bestAngle || (delta == bestAngle && di < bestIntensity)) {
      best = i;
      bestAngle = delta;
      bestIntensity = di;
    }
  }
  return best;
}
Pose Art::pose(std::string name, const Expression &em, bool flip) {
  if (!data["characters"].contains(name))
    name = "anna";
  auto &ch = data["characters"][name];
  auto &b = ch["type"] == 1 ? ch["bodies"] : ch["torsos"];
  auto &f = ch["faces"];
  int bi = em.cooked && em.torso >= 0 && em.torso < int(b.size())
               ? em.torso
               : choose(b, em.gesture == 9 ? em.emotion : em.gesture,
                        em.gesture == 9 ? em.intensity : em.gestureIntensity);
  int fi = ch["type"] == 1 ? bi
           : em.cooked && em.face >= 0 && em.face < int(f.size())
               ? em.face
               : choose(f, em.emotion, em.intensity);
  std::string key = name + ":" + std::to_string(fi) + ":" + std::to_string(bi) +
                    ":" + std::to_string(flip);
  if (poses.count(key))
    return poses.at(key);
  if (bi < 0 || fi < 0)
    throw std::runtime_error("Character has no usable poses");
  Pose out;
  out.face = ch["type"] == 1 ? 0 : fi;
  out.torso = bi;
  auto body = image(b[bi]["image"]);
  out.bodyEmotion = b[bi]["emotion"];
  out.bodyIntensity = b[bi]["intensity"];
  if (ch["type"] == 1) {
    out.image = body;
    out.headHeight = body->h / 2;
    out.faceX = b[bi]["coords"][0];
    out.faceEmotion = out.bodyEmotion;
    out.faceIntensity = out.bodyIntensity;
  } else {
    auto head = image(f[fi]["image"]);
    auto bc = b[bi]["coords"].get<std::vector<int>>(),
         fc = f[fi]["coords"].get<std::vector<int>>();
    int x = bc[0] + fc[2] - fc[0], y = bc[1] + fc[3] - fc[1],
        left = std::min(0, x), top = std::min(0, y);
    out.image = std::make_shared<Image>(std::max(body->w, x + head->w) - left,
                                        std::max(body->h, y + head->h) - top);
    // Original renderer draws both white auras first, then orders the masked
    // art.
    for (auto pair : {std::pair{body, b[bi]["image"].get<std::string>()},
                      std::pair{head, f[fi]["image"].get<std::string>()}}) {
      auto aura = image(pair.second + ".aura.png");
      Image white(aura->w, aura->h);
      for (size_t n = 0; n < white.rgba.size(); n += 4) {
        white.rgba[n] = white.rgba[n + 1] = white.rgba[n + 2] = 255;
        white.rgba[n + 3] = aura->rgba[n];
      }
      out.image->over(white, pair.first == body ? -left : x - left,
                      pair.first == body ? -top : y - top);
    }
    if (ch["flags"].get<int>() & 4) {
      out.image->over(*body, -left, -top);
      out.image->over(*head, x - left, y - top);
    } else {
      out.image->over(*head, x - left, y - top);
      out.image->over(*body, -left, -top);
    }
    out.headHeight = y + head->h - top;
    out.faceX = fc[4] + x - left;
    out.faceEmotion = f[fi]["emotion"];
    out.faceIntensity = f[fi]["intensity"];
  }
  if (flip) {
    out.image = std::make_shared<Image>(out.image->flipped());
    out.faceX = out.image->w - out.faceX;
  }
  poses[key] = out;
  return out;
}
} // namespace comic
