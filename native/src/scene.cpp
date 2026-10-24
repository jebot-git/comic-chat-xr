// SPDX-License-Identifier: MIT
#include "comic.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <png.h>
#include <stdexcept>
#include <unistd.h>
namespace comic {
namespace {
struct Vertex {
  float x, y, z, nx, ny, nz, u, v;
};
struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};
void quad(Mesh &m, std::array<Vertex, 4> q) {
  auto base = m.vertices.size();
  m.vertices.insert(m.vertices.end(), q.begin(), q.end());
  for (auto i : {0, 1, 2, 0, 2, 3})
    m.indices.push_back(base + i);
}
struct Glb {
  Json j = {{"asset", {{"version", "2.0"}, {"generator", "de-xr Comic Chat"}}},
            {"extensionsUsed", {"KHR_materials_unlit"}},
            {"scene", 0},
            {"scenes", Json::array({{{"nodes", Json::array()}}})},
            {"nodes", Json::array()},
            {"meshes", Json::array()},
            {"materials", Json::array()},
            {"textures", Json::array()},
            {"images", Json::array()},
            {"samplers", Json::array({{{"magFilter", 9728},
                                       {"minFilter", 9728},
                                       {"wrapS", 33071},
                                       {"wrapT", 33071}}})},
            {"bufferViews", Json::array()},
            {"accessors", Json::array()}};
  std::vector<unsigned char> bytes;
  int view(const void *p, size_t n, int stride = 0) {
    while (bytes.size() % 4)
      bytes.push_back(0);
    int id = j["bufferViews"].size();
    Json v = {{"buffer", 0}, {"byteOffset", bytes.size()}, {"byteLength", n}};
    if (stride)
      v["byteStride"] = stride;
    j["bufferViews"].push_back(v);
    auto *b = static_cast<const unsigned char *>(p);
    bytes.insert(bytes.end(), b, b + n);
    return id;
  }
  int material(const Image &im, bool readableText = false) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    png.width = im.w;
    png.height = im.h;
    png.format = PNG_FORMAT_RGBA;
    png_alloc_size_t n = 0;
    if (!png_image_write_to_memory(&png, nullptr, &n, 0, im.rgba.data(), 0,
                                   nullptr))
      throw std::runtime_error("PNG encode failed");
    std::vector<unsigned char> encoded(n);
    if (!png_image_write_to_memory(&png, encoded.data(), &n, 0, im.rgba.data(),
                                   0, nullptr))
      throw std::runtime_error("PNG encode failed");
    int v = view(encoded.data(), n), id = j["materials"].size();
    j["images"].push_back({{"bufferView", v}, {"mimeType", "image/png"}});
    int sampler = 0;
    if (readableText) {
      sampler = j["samplers"].size();
      j["samplers"].push_back({{"magFilter", 9729},
                               {"minFilter", 9987},
                               {"wrapS", 33071},
                               {"wrapT", 33071}});
    }
    j["textures"].push_back({{"sampler", sampler}, {"source", id}});
    j["materials"].push_back(
        {{"name", "original-art"},
         {"pbrMetallicRoughness",
          {{"baseColorTexture", {{"index", id}}},
           {"metallicFactor", 0},
           {"roughnessFactor", 1}}},
         {"extensions", {{"KHR_materials_unlit", Json::object()}}},
         {"alphaMode", "MASK"},
         {"alphaCutoff", .5},
         {"doubleSided", true}});
    return id;
  }
  int access(int v, int offset, int component, int count, const char *type,
             Json min = nullptr, Json max = nullptr) {
    Json a = {{"bufferView", v},
              {"byteOffset", offset},
              {"componentType", component},
              {"count", count},
              {"type", type}};
    if (!min.is_null()) {
      a["min"] = min;
      a["max"] = max;
    }
    int id = j["accessors"].size();
    j["accessors"].push_back(a);
    return id;
  }
  void mesh(const Mesh &mesh, int material, const std::string &name,
            Json extras, Json translation = {0, 0, 0},
            Json rotation = {0, 0, 0, 1}) {
    if (mesh.vertices.empty() || mesh.indices.empty())
      return;
    std::array<float, 3> mn{1e20f, 1e20f, 1e20f}, mx{-1e20f, -1e20f, -1e20f};
    for (auto &v : mesh.vertices) {
      std::array<float, 3> p{v.x, v.y, v.z};
      for (int c = 0; c < 3; ++c) {
        mn[c] = std::min(mn[c], p[c]);
        mx[c] = std::max(mx[c], p[c]);
      }
    }
    int v = view(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex),
                 sizeof(Vertex));
    int pos = access(v, 0, 5126, mesh.vertices.size(), "VEC3", mn, mx),
        norm = access(v, 12, 5126, mesh.vertices.size(), "VEC3"),
        uv = access(v, 24, 5126, mesh.vertices.size(), "VEC2");
    int iv = view(mesh.indices.data(), mesh.indices.size() * 4),
        ind = access(iv, 0, 5125, mesh.indices.size(), "SCALAR");
    int id = j["nodes"].size();
    int mid = j["meshes"].size();
    j["meshes"].push_back(
        {{"name", name},
         {"primitives",
          Json::array(
              {{{"attributes",
                 {{"POSITION", pos}, {"NORMAL", norm}, {"TEXCOORD_0", uv}}},
                {"indices", ind},
                {"material", material}}})}});
    j["nodes"].push_back({{"name", name},
                          {"mesh", mid},
                          {"translation", translation},
                          {"rotation", rotation},
                          {"extras", extras}});
    j["scenes"][0]["nodes"].push_back(id);
  }
  void save(const fs::path &path) {
    while (bytes.size() % 4)
      bytes.push_back(0);
    j["buffers"] = Json::array({{{"byteLength", bytes.size()}}});
    auto json = j.dump();
    while (json.size() % 4)
      json += ' ';
    uint32_t length = 12 + 8 + json.size() + 8 + bytes.size();
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(getpid());
    std::ofstream out(temporary, std::ios::binary);
    auto u32 = [&](uint32_t n) {
      unsigned char b[] = {static_cast<unsigned char>(n),
                           static_cast<unsigned char>(n >> 8),
                           static_cast<unsigned char>(n >> 16),
                           static_cast<unsigned char>(n >> 24)};
      out.write(reinterpret_cast<char *>(b), 4);
    };
    u32(0x46546c67);
    u32(2);
    u32(length);
    u32(json.size());
    u32(0x4e4f534a);
    out.write(json.data(), json.size());
    u32(bytes.size());
    u32(0x004e4942);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    out.close();
    if (!out)
      throw std::runtime_error("Cannot write GLB");
    fs::rename(temporary, path);
  }
};
Mesh cutout(const Image &im, float width, float height, float depth,
            float visible) {
  Mesh mesh;
  auto solid = [&](int x, int y) {
    return x >= 0 && x < im.w && y >= 0 && y < im.h &&
           y < float(im.h) * visible &&
           im.rgba[(size_t(y) * im.w + x) * 4 + 3] >= 128;
  };
  auto point = [&](float x, float y, float z, float nx, float ny, float nz) {
    return Vertex{(x / im.w - .5f) * width,
                  (.5f - y / im.h) * height,
                  z,
                  nx,
                  ny,
                  nz,
                  x / im.w,
                  y / im.h};
  };
  float front = depth / 2, back = -depth / 2;
  for (int y = 0; y < im.h * visible; ++y) {
    for (int x = 0; x < im.w;) {
      if (!solid(x, y)) {
        ++x;
        continue;
      }
      int start = x;
      while (solid(x, y))
        ++x;
      quad(mesh,
           {point(start, y, front, 0, 0, 1),
            point(start, y + 1, front, 0, 0, 1),
            point(x, y + 1, front, 0, 0, 1), point(x, y, front, 0, 0, 1)});
      quad(mesh, {point(x, y, back, 0, 0, -1), point(x, y + 1, back, 0, 0, -1),
                  point(start, y + 1, back, 0, 0, -1),
                  point(start, y, back, 0, 0, -1)});
    }
    for (int x = 0; x < im.w; ++x)
      if (solid(x, y)) {
        auto wall = [&](int ax, int ay, int bx, int by, int nx, int ny) {
          std::array<Vertex, 4> q{
              point(ax, ay, front, nx, ny, 0), point(bx, by, front, nx, ny, 0),
              point(bx, by, back, nx, ny, 0), point(ax, ay, back, nx, ny, 0)};
          // Sample the solid boundary texel on all sidewall vertices. Sampling
          // outside the contour would alpha-mask away the physical depth.
          for (auto &v : q) {
            v.u = (x + .5f) / im.w;
            v.v = (y + .5f) / im.h;
          }
          quad(mesh, q);
        };
        if (!solid(x - 1, y))
          wall(x, y + 1, x, y, -1, 0);
        if (!solid(x + 1, y))
          wall(x + 1, y, x + 1, y + 1, 1, 0);
        if (!solid(x, y - 1))
          wall(x, y, x + 1, y, 0, 1);
        if (!solid(x, y + 1))
          wall(x + 1, y + 1, x, y + 1, 0, -1);
      }
  }
  return mesh;
}
void background(Glb &out, Art &art, const std::string &name) {
  auto &info = art.catalog().at("backdrops").at(name);
  auto definitions = info.value(
      "layers",
      Json::array(
          {{{"name", "far"}, {"depth", 7.0}, {"image", info.at("image")}}}));
  for (auto &layer : definitions) {
    float depth = layer.at("depth");
    float extent = depth * 1.15f;
    Mesh plane;
    quad(plane,
         std::array<Vertex, 4>{{{-extent, extent, 0, 0, 0, 1, -.075f, -.075f},
                                {-extent, -extent, 0, 0, 0, 1, -.075f, 1.075f},
                                {extent, -extent, 0, 0, 0, 1, 1.075f, 1.075f},
                                {extent, extent, 0, 0, 0, 1, 1.075f, -.075f}}});
    auto image = art.image(layer.at("image"));
    out.mesh(plane, out.material(*image),
             name + " / " + layer.at("name").get<std::string>(),
             {{"role", "backdrop"},
              {"projection", "parallax-layers"},
              {"layer", layer.at("name")},
              {"depthMetres", depth},
              {"source", name + ".bgb"}},
             {0, 1.6, -depth});
  }
}

} // namespace
void exportScene(const Panel &panel, Art &art, const fs::path &path,
                 const std::string &font, int size) {
  // Grow short messages without overlapping the character area or squeezing
  // dense panels into smaller type. Rasterization density is independent of
  // angular font size; artwork keeps its original nearest-neighbor sampler.
  int textSize = std::max(size, 24);
  while (textSize > size && !balloonsFit(panel, font, textSize))
    --textSize;
  constexpr int textRasterScale = 3;
  auto rendered = render(panel, art, font, textSize, textRasterScale);
  Glb out;
  out.j["extras"] = {
      {"application", "Comic Chat XR"},
      {"panelSize", PANEL},
      {"coordinateSystem", "OpenXR local metres; viewer at (0,1.6,0)"},
      {"upstream", "48a162249484ab8d116c243e8203b0956d350c09"},
      {"depthMetres", .035}};
  background(out, art, panel.backdrop);
  if (panel.messages.empty()) {
    if (!path.parent_path().empty())
      fs::create_directories(path.parent_path());
    out.save(path);
    return;
  }
  for (auto &a : rendered.layout.actors) {
    float cx = (a.x + a.w / 2) / PANEL, cy = (a.y + a.h / 2) / PANEL;
    // Rectilinear front view preserves the comic layout exactly at the
    // reference eye. Each participant has its own room-anchored mesh; depth is
    // never flattened.
    // Put the visible face on the reference plane; extrusion extends behind it.
    float px = (cx - .5f) * 4, py = 1.6f + (.5f - cy) * 4, pz = -2.0175f;
    float visible = std::min(1.f, (PANEL - a.y) / a.h);
    auto mesh =
        cutout(*a.pose.image, a.w / PANEL * 4, a.h / PANEL * 4, .035f, visible);
    out.mesh(mesh, out.material(*a.pose.image), a.nick,
             {{"role", "character"},
              {"character", a.character},
              {"face", a.pose.face},
              {"torso", a.pose.torso},
              {"flipped", a.flip},
              {"desktopRect", {a.x, a.y, a.w, a.h}},
              {"depthMetres", .035}},
             {px, py, pz});
  }
  // Text bubbles are separate spatial geometry in front of the speakers.
  // Keep the same reference projection while placing balloons just in front.
  constexpr float balloonDepth = 1.94f;
  auto balloon =
      cutout(rendered.balloons, 2 * balloonDepth, 2 * balloonDepth, .004, 1);
  out.mesh(balloon, out.material(rendered.balloons, true), "Speech balloons",
           {{"role", "balloons"}, {"fontSize", textSize},
            {"rasterScale", textRasterScale}, {"referenceDepthMetres", balloonDepth}},
           {0, 1.6, -balloonDepth - .002f});
  if (!path.parent_path().empty())
    fs::create_directories(path.parent_path());
  out.save(path);
}
void exportCharacter(const std::string &name, const Expression &e, Art &art,
                     const fs::path &path) {
  if (!art.catalog()["characters"].contains(name))
    throw std::runtime_error("Unknown character: " + name);
  auto pose = art.pose(name, e);
  Glb out;
  int left = pose.image->w, top = pose.image->h, right = 0, bottom = 0;
  for (int y = 0; y < pose.image->h; ++y)
    for (int x = 0; x < pose.image->w; ++x)
      if (pose.image->rgba[(size_t(y) * pose.image->w + x) * 4 + 3] >= 128) {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x + 1);
        bottom = std::max(bottom, y + 1);
      }
  if (right <= left || bottom <= top)
    throw std::runtime_error("Character pose has no opaque artwork");
  Image trimmed(right - left, bottom - top);
  for (int y = 0; y < trimmed.h; ++y)
    std::copy_n(pose.image->rgba.data() +
                    (size_t(y + top) * pose.image->w + left) * 4,
                trimmed.w * 4, trimmed.rgba.data() + size_t(y) * trimmed.w * 4);
  float height = 1.65f, width = height * trimmed.w / trimmed.h;
  auto mesh = cutout(trimmed, width, height, .035f, 1);
  out.mesh(mesh, out.material(trimmed), name,
           {{"role", "character"},
            {"character", name},
            {"face", pose.face},
            {"torso", pose.torso},
            {"emotion", e.emotion},
            {"intensity", e.intensity},
            {"depthMetres", .035}},
           {0, height / 2, 0});
  if (!path.parent_path().empty())
    fs::create_directories(path.parent_path());
  out.save(path);
}
void exportAssets(Art &art, const fs::path &directory) {
  fs::create_directories(directory / "characters");
  fs::create_directories(directory / "backdrops");
  Json manifest = {{"format", 2},
                   {"backgroundProjection", "parallax-layers"},
                   {"characters", Json::object()},
                   {"backdrops", Json::object()},
                   {"depthMetres", .035},
                   {"upstream", "48a162249484ab8d116c243e8203b0956d350c09"}};
  Expression neutral;
  for (auto &name : art.characters()) {
    exportCharacter(name, neutral, art,
                    directory / "characters" / (name + ".glb"));
    manifest["characters"][name] = {
        {"neutral", "characters/" + name + ".glb"},
        {"source", art.catalog()["characters"][name]},
        {"expressions",
         "Generate any original pose with --character NAME --expression INDEX "
         "--intensity 0..1 --export-character FILE.glb"}};
  }
  for (auto &name : art.backdrops()) {
    Glb out;
    background(out, art, name);
    out.save(directory / "backdrops" / (name + ".glb"));
    manifest["backdrops"][name] = {
        {"scene", "backdrops/" + name + ".glb"},
        {"source", art.catalog()["backdrops"][name]}};
  }
  std::ofstream(directory / "manifest.json") << manifest.dump(2) << '\n';
}
} // namespace comic
