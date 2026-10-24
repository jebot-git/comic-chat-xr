// SPDX-License-Identifier: MIT
#define GL_GLEXT_PROTOTYPES
#include "appearance.h"
#include "comic.h"
#include "desktop-menu.h"
#include "domed.h"
#include "gltf-render.h"
#include "gpu-window.h"
#include "irc.h"
#include "model-loader.h"
#include "model-xr.h"
#include "window-stereo.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/fl_ask.H>
#include <GL/glext.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>
using namespace comic;
namespace {
const std::array<const char *, 18> emotions = {
    "Auto",      "Happy",       "Coy",        "Bored",        "Scared",
    "Sad",       "Angry",       "Shout",      "Laugh",        "Neutral",
    "Wave",      "Point other", "Point self", "Double point", "Shrug",
    "Walk rear", "Walk side",   "Walk front"};
struct Options {
  fs::path art, transcript, render, exportPath, xr, report, capture,
      probeOutput, sceneCapture, exportLibrary, exportOne;
  std::string characterName = "anna", realm = "inherit",
              resolvedRealm = "standalone";
  int emotion = 9;
  float intensity = 0;
  bool sessionStatus = false;
  bool demo = false, xrProbe = false, ircProbe = false, tls = true;
  std::string host = "127.0.0.1", nick = "ComicXR", channel = "#comicchat";
  double seconds = 0;
  unsigned port = 6697;
};
Options options(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&]() {
      if (++i >= argc)
        throw std::runtime_error("Missing value after " + a);
      return std::string(argv[i]);
    };
    if (a == "--session-status")
      o.sessionStatus = true;
    else if (a == "--realm")
      o.realm = value();
    else if (a == "--art-dir")
      o.art = value();
    else if (a == "--demo")
      o.demo = true;
    else if (a == "--transcript")
      o.transcript = value();
    else if (a == "--render")
      o.render = value();
    else if (a == "--export-assets")
      o.exportLibrary = value();
    else if (a == "--export-character")
      o.exportOne = value();
    else if (a == "--character")
      o.characterName = value();
    else if (a == "--expression")
      o.emotion = std::stoi(value());
    else if (a == "--intensity")
      o.intensity = std::stof(value());
    else if (a == "--export")
      o.exportPath = value();
    else if (a == "--xr")
      o.xr = value();
    else if (a == "--report")
      o.report = value();
    else if (a == "--seconds")
      o.seconds = std::stod(value());
    else if (a == "--xr-probe")
      o.xrProbe = true;
    else if (a == "--capture-ui")
      o.capture = value();
    else if (a == "--capture-scene")
      o.sceneCapture = value();
    else if (a == "--irc-probe") {
      o.ircProbe = true;
      o.host = value();
      o.port = std::stoul(value());
    } else if (a == "--no-tls")
      o.tls = false;
    else if (a == "--nick")
      o.nick = value();
    else if (a == "--channel")
      o.channel = value();
    else if (a == "--probe-output")
      o.probeOutput = value();
    else if (a == "--help") {
      std::cout
          << "Comic Chat XR — native FLTK / Wayland / OpenXR\n  --demo  "
             "--art-dir DIR  --transcript FILE\n  --render PANEL.png  --export "
             "SCENE.glb\n  --xr SCENE.glb [--seconds N] [--report FILE]\n  "
             "--xr-probe  --capture-ui FILE.png  --capture-scene FILE.ppm\n"
             "  --export-assets DIR\n  --character NAME --expression 1..17 "
             "--intensity 0..1 --export-character FILE.glb\n";
      std::exit(0);
    } else
      throw std::runtime_error("Unknown option: " + a);
  }
  if (o.emotion < 0 || o.emotion > 17 || !std::isfinite(o.intensity) ||
      o.intensity < 0 || o.intensity > 1)
    throw std::runtime_error("Expression or intensity out of range");
  if (!std::isfinite(o.seconds) || o.seconds < 0)
    throw std::runtime_error("Invalid duration");
  if (o.art.empty()) {
    const char *env = getenv("COMIC_CHAT_ART");
    if (env)
      o.art = env;
    else {
      std::error_code error;
      auto exe = fs::read_symlink("/proc/self/exe", error);
      auto relative =
          exe.parent_path().parent_path() / "share/comic-chat-xr/art";
      if (!error && fs::exists(relative / "catalog.json"))
        o.art = relative;
      else if (fs::exists(COMIC_ART_INSTALL "/catalog.json"))
        o.art = COMIC_ART_INSTALL;
      else
        o.art = COMIC_ART_BUILD;
    }
  }
  return o;
}
class SceneWriter {
  struct State {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false, pending = false, busy = false;
    Panel panel;
    fs::path output, cleanup;
    ~State() {
      if (!cleanup.empty()) {
        std::error_code error;
        fs::remove_all(cleanup, error);
      }
    }
    std::string font = "monospace", error;
    int size = 18;
    uint64_t generation = 0, finished = 0;
  };
  std::shared_ptr<State> state = std::make_shared<State>();

public:
  explicit SceneWriter(fs::path art) {
    auto s = state;
    std::thread([s, art = std::move(art)] {
      try {
        Art assets(art);
        for (;;) {
          Panel p;
          fs::path path;
          std::string font;
          int size;
          uint64_t gen;
          {
            std::unique_lock lock(s->mutex);
            s->wake.wait(lock, [&] { return s->stop || s->pending; });
            if (s->stop)
              return;
            p = s->panel;
            path = s->output;
            font = s->font;
            size = s->size;
            gen = s->generation;
            s->pending = false;
            s->busy = true;
          }
          std::string error;
          try {
            exportScene(p, assets, path, font, size);
          } catch (const std::exception &e) {
            error = e.what();
          }
          {
            std::lock_guard lock(s->mutex);
            s->busy = false;
            s->finished = gen;
            s->error = error;
          }
        }
      } catch (const std::exception &e) {
        std::lock_guard lock(s->mutex);
        s->error = e.what();
        s->busy = false;
      }
    }).detach();
  }
  ~SceneWriter() {
    std::lock_guard lock(state->mutex);
    state->stop = true;
    state->wake.notify_one();
  }
  void cleanupOnExit(const fs::path &path) {
    std::lock_guard lock(state->mutex);
    state->cleanup = path;
  }
  void request(const Panel &p, const fs::path &path, const std::string &font,
               int size) {
    std::lock_guard lock(state->mutex);
    state->error.clear();
    state->panel = p;
    state->output = path;
    state->font = font;
    state->size = size;
    ++state->generation;
    state->pending = true;
    state->wake.notify_one();
  }
  bool ready(std::string &error) {
    std::lock_guard lock(state->mutex);
    error = state->error;
    return !state->pending && !state->busy && state->generation > 0 &&
           state->finished == state->generation && error.empty();
  }
};
// FLTK's GL widget driver has no Fl_RGB_Image implementation. Keep immutable
// comic pixels in GPU textures instead of relying on the driver's image stub.
class TextureImage {
  std::shared_ptr<Image> pixels;
  GLuint texture = 0;
  Fl_Gl_Window *owner = nullptr;
  GLContext context = nullptr;

public:
  explicit TextureImage(std::shared_ptr<Image> image)
      : pixels(std::move(image)) {}
  ~TextureImage() {
    if (texture && owner && owner->shown() && owner->context() == context) {
      owner->make_current();
      glDeleteTextures(1, &texture);
    }
  }
  void draw(Fl_Widget &widget, int x, int y, int w, int h) {
    owner = dynamic_cast<Fl_Gl_Window *>(widget.window());
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT |
                 GL_CURRENT_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    if (!texture || context != owner->context() || !glIsTexture(texture)) {
      glGenTextures(1, &texture);
      context = owner->context();
      glBindTexture(GL_TEXTURE_2D, texture);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pixels->w, pixels->h, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, pixels->rgba.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else
      glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(x, y);
    glTexCoord2f(1, 0);
    glVertex2f(x + w, y);
    glTexCoord2f(1, 1);
    glVertex2f(x + w, y + h);
    glTexCoord2f(0, 1);
    glVertex2f(x, y + h);
    glEnd();
    glPopClientAttrib();
    glPopAttrib();
  }
};
class ComicPage : public Fl_Widget {
  std::vector<std::shared_ptr<Image>> panels;
  std::vector<std::unique_ptr<TextureImage>> images;
  std::vector<std::string> signatures;
  std::unique_ptr<TextureImage> idleBackdrop;
  int offset = 0, columns = 2, selected = -1;
  std::function<void(int)> selection;

public:
  ComicPage(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
  void update(Document &doc, Art &art, const std::string &font, int size) {
    // Reuse unchanged historical panels and their GPU textures. A new line
    // only rasterizes the panel it changes; retaining 200 panels never makes
    // each incoming message redraw 200 copies on the FLTK thread.
    std::map<std::string, size_t> old;
    for (size_t i = 0; i < signatures.size(); ++i)
      old[signatures[i]] = i;
    std::vector<std::shared_ptr<Image>> nextPanels;
    std::vector<std::unique_ptr<TextureImage>> nextImages;
    std::vector<std::string> nextSignatures;
    auto snapshot = doc.json().at("panels");
    for (size_t i = 0; i < doc.panels.size(); ++i) {
      auto key = snapshot[i].dump() + font + std::to_string(size);
      auto previous = old.find(key);
      if (previous != old.end() && images[previous->second]) {
        nextPanels.push_back(panels[previous->second]);
        nextImages.push_back(std::move(images[previous->second]));
      } else {
        auto image = std::make_shared<Image>(
            render(doc.panels[i], art, font, size).panel);
        nextPanels.push_back(image);
        nextImages.push_back(std::make_unique<TextureImage>(image));
      }
      nextSignatures.push_back(std::move(key));
    }
    panels = std::move(nextPanels);
    images = std::move(nextImages);
    signatures = std::move(nextSignatures);
    if (panels.empty()) {
      auto idle = std::make_shared<Image>(
          render(Panel{{}, doc.backdrop}, art, font, size).panel);
      idleBackdrop = std::make_unique<TextureImage>(idle);
    } else
      idleBackdrop.reset();
    selected = panels.empty() ? -1 : panels.size() - 1;
    offset = std::max(0, contentHeight() - h());
    redraw();
  }
  int cell() const { return std::max(200, (w() - 20) / std::max(1, columns)); }
  int contentHeight() const {
    return ((images.size() + columns - 1) / columns) * cell() + 16;
  }
  int current() const { return selected; }
  void onSelect(std::function<void(int)> fn) { selection = std::move(fn); }
  void draw() override {
    columns = std::max(1, w() / 340);
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_WHITE);
    fl_rectf(x(), y(), w(), h());
    if (images.empty() && idleBackdrop) {
      int side = std::max(1, std::min(w() - 16, h() - 16));
      idleBackdrop->draw(*this, x() + (w() - side) / 2, y() + 8, side, side);
    }
    int side = cell() - 10;
    for (size_t i = 0; i < images.size(); ++i) {
      int px = x() + 8 + (i % columns) * cell(),
          py = y() + 8 + (i / columns) * cell() - offset;
      if (py + side < y() || py > y() + h())
        continue;
      images[i]->draw(*this, px, py, side, side);
      if (int(i) == selected) {
        fl_color(FL_SELECTION_COLOR);
        fl_rect(px - 2, py - 2, side + 4, side + 4);
      }
    }
    fl_pop_clip();
  }
  int handle(int event) override {
    if (event == FL_MOUSEWHEEL) {
      offset = std::clamp(offset + Fl::event_dy() * 45, 0,
                          std::max(0, contentHeight() - h()));
      redraw();
      return 1;
    }
    if (event == FL_PUSH) {
      int col = (Fl::event_x() - x() - 8) / cell(),
          row = (Fl::event_y() - y() - 8 + offset) / cell();
      int item = row * columns + col;
      if (item >= 0 && item < int(images.size())) {
        selected = item;
        redraw();
        if (selection)
          selection(item);
      }
      return 1;
    }
    return Fl_Widget::handle(event);
  }
};
class EmotionWheel : public Fl_Widget {
  std::function<void(int, float)> changed;
  int emotion = 9;
  float intensity = 0;

public:
  EmotionWheel(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
  void onChange(std::function<void(int, float)> fn) { changed = std::move(fn); }
  void value(int e, float i) {
    emotion = e;
    intensity = i;
    redraw();
  }
  void draw() override {
    int cx = x() + w() / 2, cy = y() + h() / 2;
    double r = std::min(w(), h()) * .40;
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_BACKGROUND2_COLOR);
    fl_pie(cx - r, cy - r, 2 * r, 2 * r, 0, 360);
    fl_color(FL_FOREGROUND_COLOR);
    fl_arc(cx - r, cy - r, 2 * r, 2 * r, 0, 360);
    fl_font(dx::font, std::max(9, dx::size - 3));
    for (int i = 0; i < 8; ++i) {
      double a = i * M_PI / 4;
      fl_line(cx, cy, cx + std::cos(a + M_PI / 8) * r,
              cy - std::sin(a + M_PI / 8) * r);
      int px = cx + std::cos(a) * r * .69, py = cy - std::sin(a) * r * .69;
      fl_draw(emotions[i + 1], px - 30, py - 7, 60, 14, FL_ALIGN_CENTER);
    }
    fl_color(FL_BACKGROUND_COLOR);
    fl_pie(cx - 19, cy - 19, 38, 38, 0, 360);
    fl_color(FL_FOREGROUND_COLOR);
    fl_draw("N", cx - 7, cy + 5);
    double a = (emotion >= 1 && emotion <= 8 ? emotion - 1 : 0) * M_PI / 4;
    int px = cx + std::cos(a) * r * intensity,
        py = cy - std::sin(a) * r * intensity;
    fl_color(FL_SELECTION_COLOR);
    fl_pie(px - 5, py - 5, 10, 10, 0, 360);
    fl_pop_clip();
  }
  int handle(int event) override {
    if (event != FL_PUSH && event != FL_DRAG)
      return Fl_Widget::handle(event);
    double dx = Fl::event_x() - x() - w() / 2,
           dy = y() + h() / 2 - Fl::event_y(), r = std::min(w(), h()) * .4;
    float v = std::min(1., std::hypot(dx, dy) / r);
    double a = std::atan2(dy, dx);
    if (a < 0)
      a += 2 * M_PI;
    int e = v < .17 ? 9 : (int(std::floor(a / (M_PI / 4) + .5)) % 8) + 1;
    if (e == 9)
      v = 0;
    value(e, v);
    if (changed)
      changed(e, v);
    return 1;
  }
};
class CharacterPreview : public Fl_Widget {
  std::shared_ptr<Image> pixels;
  std::unique_ptr<TextureImage> image;

public:
  CharacterPreview(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
  void update(Pose p) {
    pixels = p.image;
    image = std::make_unique<TextureImage>(pixels);
    redraw();
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_WHITE);
    fl_rectf(x(), y(), w(), h());
    if (image) {
      double scale =
          std::min(double(w() - 8) / pixels->w, double(h() - 8) / pixels->h);
      int iw = pixels->w * scale, ih = pixels->h * scale;
      image->draw(*this, x() + (w() - iw) / 2, y() + h() - ih - 4, iw, ih);
    }
    fl_pop_clip();
  }
};
// GPU targets belong to the parent window's context. No child surface escapes
// the window, and no CPU image copy is used for stereo transport.
class RenderTarget {
  GLuint framebuffer = 0, texture = 0, depth = 0;
  int width = 0, height = 0;

public:
  void abandon() {
    framebuffer = texture = depth = 0;
    width = height = 0;
  }
  void release() {
    if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
    if (texture)
      glDeleteTextures(1, &texture);
    if (depth)
      glDeleteRenderbuffers(1, &depth);
    abandon();
  }
  GLuint colorTexture() const { return texture; }
  GLuint get(int w, int h) {
    if (w == width && h == height && framebuffer)
      return framebuffer;
    release();
    width = w;
    height = h;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("Incomplete stereo framebuffer");
    return framebuffer;
  }
};
class SceneView : public Fl_Widget {
  fsv::gltf::Loader loader;
  fsv::gltf::Renderer renderer;
  std::shared_ptr<const fsv::gltf::Model> model;
  RenderTarget chrome, eyeTarget;
  DomeMode mode;
  float offsetX = 0, offsetY = 0;
  int downX = 0, downY = 0;

public:
  size_t frames = 0, stereoFrames = 0;
  float contentHeight = 0;
  std::string error;
  SceneView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
  void abandon() {
    renderer.abandon();
    chrome.abandon();
    eyeTarget.abandon();
  }
  void release() {
    chrome.release();
    eyeTarget.release();
  }
  void open(const fs::path &path) { loader.open(path); }
  void reset() {
    offsetX = offsetY = 0;
    window()->redraw();
  }
  void advance(const DomeMode &value) {
    mode = value;
    auto snapshot = loader.snapshot();
    model = snapshot.model;
    if (!snapshot.error.empty())
      error = snapshot.error;
    if (visible_r())
      window()->redraw();
  }
  void draw() override {} // Draw after the parent has finished its mono chrome.
  int handle(int event) override {
    if (event == FL_PUSH) {
      downX = Fl::event_x();
      downY = Fl::event_y();
      return 1;
    }
    if (event == FL_DRAG) {
      offsetX =
          std::clamp(offsetX + (Fl::event_x() - downX) * .001f, -.15f, .15f);
      offsetY =
          std::clamp(offsetY - (Fl::event_y() - downY) * .001f, -.15f, .15f);
      downX = Fl::event_x();
      downY = Fl::event_y();
      window()->redraw();
      return 1;
    }
    return Fl_Widget::handle(event);
  }
  void composite(int logicalW, int logicalH, int pixelsW, int pixelsH,
                 bool renderScene = true) {
    using namespace fsv::gltf;
    GLint original = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &original);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_SCISSOR_TEST);
    try {
      bool stereo = mode.stereoSupported;
      // Copy all chrome once, then squeeze an identical copy into each eye.
      // Input stays in the unsqueezed logical window coordinate system.
      if (stereo) {
        GLuint copy = chrome.get(pixelsW, pixelsH);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, original);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copy);
        glBlitFramebuffer(0, 0, pixelsW, pixelsH, 0, 0, pixelsW, pixelsH,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, copy);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, original);
        int half = pixelsW / 2;
        glBlitFramebuffer(0, 0, pixelsW, pixelsH, 0, 0, half, pixelsH,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBlitFramebuffer(0, 0, pixelsW, pixelsH, pixelsW - half, 0, pixelsW,
                          pixelsH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      }
      if (!renderScene) {
        glBindFramebuffer(GL_FRAMEBUFFER, original);
        glPopAttrib();
        return;
      }
      int pw = std::max(1, w() * pixelsW / logicalW),
          ph = std::max(1, h() * pixelsH / logicalH);
      GLuint target = eyeTarget.get(pw, ph);
      float distance = mode.pulloutDistance;
      float windowH = distance * mode.laneHeightRadians;
      float windowW = windowH * logicalW / logicalH;
      if (mode.hasWindowQuad) {
        windowW = mode.windowSize[0];
        windowH = mode.windowSize[1];
      }
      // Restrict content to the lane even when a pulled-out window is taller.
      float laneFraction =
          std::min(1.f, distance * mode.laneHeightRadians / windowH);
      float height = windowH * float(h()) / logicalH;
      float width = windowW * float(w()) / logicalW;
      contentHeight = .96f * std::min(width, height * laneFraction);
      float scale = contentHeight / 4;
      auto root = trs({0, 1.6f, 0}, {}, {scale, scale, distance / 2}) *
                  trs({0, -1.6f, 0}, {});
      float centreX = windowW * ((x() + w() / 2.f) / logicalW - .5f);
      float centreY = windowH * (.5f - (y() + h() / 2.f) / logicalH);
      CylinderSurface cylinder;
      fsv::gltf::Pose tangent;
      if (mode.hasCylinder) {
        auto p = mode.cylinderPosition;
        auto q = mode.cylinderOrientation;
        cylinder = {{{p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}, 1},
                    mode.cylinderRadius,
                    mode.cylinderHorizontal,
                    mode.cylinderTop,
                    mode.cylinderBottom};
        tangent = cylinder.tangent((x() + w() / 2.f) / logicalW,
                                   (y() + h() / 2.f) / logicalH);
      }
      auto surfacePoint = [&](float u, float v) {
        auto p = cylinder.point((x() + u * w()) / logicalW,
                                (y() + v * h()) / logicalH);
        auto local = transform(inverse(tangent), p);
        return Vec3{local.x, 1.6f + local.y, local.z - distance};
      };
      int count = stereo ? 2 : 1;
      if (model)
        renderer.prepare(model, 0);
      for (int eye = 0; eye < count; ++eye) {
        Vec3 relative{stereo ? (eye ? .032f : -.032f) : 0, 0, 0};
        if (stereo && mode.hasWindowQuad) {
          auto p = mode.windowPosition;
          auto q = mode.windowOrientation;
          fsv::gltf::Pose panel{
              {p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}, 1};
          auto e = mode.eyes[eye];
          Vec3 worldEye{e[0], e[1], e[2]};
          if (!mode.trackedEyes) {
            auto p = mode.headPosition;
            auto q = mode.headOrientation;
            fsv::gltf::Pose head{
                {p[0], p[1], p[2]}, {q[0], q[1], q[2], q[3]}, 1};
            worldEye = transform(head, {eye ? .032f : -.032f, 0, 0});
          }
          auto local =
              transform(inverse(mode.hasCylinder ? tangent : panel), worldEye);
          relative = {local.x - (mode.hasCylinder ? 0 : centreX),
                      local.y - (mode.hasCylinder ? 0 : centreY),
                      local.z - distance};
        }
        relative.x += offsetX;
        relative.y += offsetY;
        auto vp = windowEyeProjection(relative, width, height, distance);
        if (mode.hasCylinder) {
          float left = 1e9f, right = -1e9f, down = 1e9f, up = -1e9f;
          Vec3 eyePosition{relative.x, 1.6f + relative.y, relative.z};
          for (int row = 0; row <= 8; ++row)
            for (int col = 0; col <= 32; ++col) {
              auto ray = surfacePoint(col / 32.f, row / 8.f) - eyePosition;
              if (ray.z >= -.02f)
                continue;
              left = std::min(left, ray.x / -ray.z);
              right = std::max(right, ray.x / -ray.z);
              down = std::min(down, ray.y / -ray.z);
              up = std::max(up, ray.y / -ray.z);
            }
          if (!(left < right && down < up))
            throw std::runtime_error("Chat surface is behind the viewer");
          vp = perspective(std::atan(left), std::atan(right), std::atan(down),
                           std::atan(up)) *
               matrix(inverse(fsv::gltf::Pose{eyePosition, {}, 1}));
        }
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        if (model)
          renderer.draw(vp, root, {relative.x, 1.6f + relative.y, relative.z},
                        pw, ph, {1, 1, 1, 1});
        else {
          glClearColor(1, 1, 1, 1);
          glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, original);
        int left = x() * pixelsW / logicalW,
            right = (x() + w()) * pixelsW / logicalW;
        if (stereo) {
          int half = pixelsW / 2, base = eye ? pixelsW - half : 0;
          left = base + left * half / pixelsW;
          right = base + right * half / pixelsW;
        }
        int bottom = (logicalH - y() - h()) * pixelsH / logicalH,
            top = (logicalH - y()) * pixelsH / logicalH;
        if (!mode.hasCylinder) {
          glBlitFramebuffer(0, 0, pw, ph, left, bottom, right, top,
                            GL_COLOR_BUFFER_BIT, GL_LINEAR);
        } else {
          // Reproject each eye through the physical cylinder. Only this content
          // rectangle is warped; chrome retains the compositor's normal UVs.
          glBindFramebuffer(GL_FRAMEBUFFER, original);
          glViewport(0, 0, pixelsW, pixelsH);
          glUseProgram(0);
          glBindVertexArray(0);
          glActiveTexture(GL_TEXTURE0);
          glBindSampler(0, 0);
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, eyeTarget.colorTexture());
          glDisable(GL_DEPTH_TEST);
          glDisable(GL_CULL_FACE);
          glDisable(GL_BLEND);
          glColor4f(1, 1, 1, 1);
          GLint previousMatrix;
          glGetIntegerv(GL_MATRIX_MODE, &previousMatrix);
          glMatrixMode(GL_PROJECTION);
          glPushMatrix();
          glLoadIdentity();
          glOrtho(0, pixelsW, 0, pixelsH, -1, 1);
          glMatrixMode(GL_MODELVIEW);
          glPushMatrix();
          glLoadIdentity();
          auto vertex = [&](float u, float v) {
            auto p = point(vp, surfacePoint(u, v));
            glTexCoord2f((p.x + 1) / 2, (p.y + 1) / 2);
            glVertex2f(left + u * (right - left), top - v * (top - bottom));
          };
          glBegin(GL_TRIANGLES);
          for (int row = 0; row < 32; ++row)
            for (int col = 0; col < 32; ++col) {
              float u = col / 32.f, v = row / 32.f, a = (col + 1) / 32.f,
                    b = (row + 1) / 32.f;
              vertex(u, v);
              vertex(a, v);
              vertex(a, b);
              vertex(u, v);
              vertex(a, b);
              vertex(u, b);
            }
          glEnd();
          glPopMatrix();
          glMatrixMode(GL_PROJECTION);
          glPopMatrix();
          glMatrixMode(previousMatrix);
        }
      }
      if (model) {
        ++frames;
        if (stereo)
          ++stereoFrames;
      }
      error.clear();
    } catch (const std::exception &e) {
      error = e.what();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, original);
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glPopAttrib();
  }
};
class Window : public dx::GpuWindow {
  Options opt;
  Art art;
  Document doc;
  Irc irc;
  SceneWriter writer;
  DomeMonitor domeMode;
  bool xrView = false, xrPresentation = false, enable3D = true,
       sceneOpened = false;
  Panel idlePanel;
  size_t xrEntries = 0, desktopReturns = 0;
  std::string modeLabel;
  Fl_Menu_Bar menu{0, 0, 1120, 28};
  Fl_Button connectButton{8, 34, 104, 30, "Connect"},
      textButton{120, 34, 104, 30, "Text view"},
      xrButton{232, 34, 130, 30, "2D view"};
  Fl_Choice character{450, 34, 160, 30, "Character"},
      backdrop{712, 34, 160, 30, "Backdrop"};
  ComicPage page{8, 74, 870, 610};
  Fl_Text_Display text{8, 74, 870, 610};
  SceneView scene{8, 74, 870, 610};
  Fl_Text_Buffer transcript;
  Fl_Hold_Browser members{888, 74, 224, 200};
  CharacterPreview preview{888, 282, 224, 180};
  EmotionWheel wheel{888, 470, 224, 170};
  Fl_Choice expression{960, 646, 152, 28, "Pose"};
  Fl_Input input{8, 694, 870, 34};
  Fl_Button sendButton{888, 694, 224, 34, "Send"};
  Fl_Output status{8, 736, 1104, 28};
  std::unique_ptr<DesktopMenu> desktop;
  std::set<std::string> users, operators;
  std::vector<std::string> characterNames, backdropNames;
  Expression selected;
  fs::path tempDir, scenePath, exportPending;
  bool registered = false;
  uint64_t fontRevision = 0;
  size_t stateSerial = 0;
  bool useText = false;
  std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now();
  static void timer(void *ptr) {
    auto *self = static_cast<Window *>(ptr);
    self->tick();
    Fl::repeat_timeout(self->xrView ? 1. / 90 : .05, timer, ptr);
  }
  static void callback(Fl_Widget *widget, void *ptr) {
    auto *self = static_cast<Window *>(ptr);
    try {
      self->action(widget);
    } catch (const std::exception &e) {
      self->status.value(e.what());
    }
  }
  std::string currentFont() {
    return dx::selected_font.family + " " + dx::selected_font.style;
  }
  void info(const std::string &s) { status.value(s.c_str()); }
  void updatePreview() {
    auto em = selected;
    if (em.emotion == 0)
      em = infer("");
    preview.update(art.pose(doc.character, em));
  }
  void updateMembers() {
    members.clear();
    for (auto &n : users)
      members.add(n.c_str());
  }
  void sync() {
    ++stateSerial;
    auto backgroundChoice =
        std::find(backdropNames.begin(), backdropNames.end(), doc.backdrop);
    if (backgroundChoice != backdropNames.end())
      backdrop.value(backgroundChoice - backdropNames.begin());
    doc.font = currentFont();
    doc.fontSize = std::clamp(dx::size, 14, 20);
    page.update(doc, art, currentFont(), std::clamp(dx::size, 14, 20));
    std::string all;
    for (auto &p : doc.panels)
      for (auto &m : p.messages) {
        if (m.continuation) {
          if (!all.empty() && all.back() == '\n')
            all.pop_back();
          all += m.text + "\n";
        } else
          all += m.nick + ": " + m.text + "\n";
      }
    transcript.text(all.c_str());
    updateMembers();
    requestScene();
  }
  const Panel *currentPanel() {
    int n = page.current();
    if (n >= 0 && n < int(doc.panels.size()))
      return &doc.panels[n];
    idlePanel.backdrop = doc.backdrop;
    return &idlePanel;
  }
  void requestScene() {
    if (auto p = currentPanel())
      writer.request(*p, scenePath, currentFont(),
                     std::clamp(dx::size, 14, 20));
  }
  void announce(const std::string &target) {
    if (!target.empty())
      irc.send("PRIVMSG " + target + " :# Appears as " + doc.character);
  }
  void process(const std::string &line) {
    auto p = parseIrc(line);
    auto nick = p.prefix.substr(0, p.prefix.find('!'));
    auto key = casefold(nick);
    if (p.command == "001") {
      registered = true;
      if (!p.args.empty())
        doc.nick = p.args[0];
      info(doc.channel.empty()
               ? "Connected — use /join #channel to enter a room."
               : "Registered; joining " + doc.channel);
    } else if (p.command == "JOIN" && !p.args.empty() &&
               casefold(p.args[0]) == casefold(doc.channel)) {
      users.insert(nick);
      if (key == casefold(doc.nick)) {
        announce(doc.channel);
        info("Joined " + doc.channel);
      }
      updateMembers();
    } else if (p.command == "353" && p.args.size() >= 4) {
      std::istringstream words(p.args.back());
      std::string name;
      while (words >> name) {
        bool op = false;
        while (!name.empty() &&
               std::string("~&@%+").find(name[0]) != std::string::npos) {
          if (name[0] != '+')
            op = true;
          name.erase(0, 1);
        }
        if (!name.empty()) {
          users.insert(name);
          if (op)
            operators.insert(casefold(name));
        }
      }
      updateMembers();
    } else if ((p.command == "PART" || p.command == "QUIT") && !nick.empty()) {
      users.erase(nick);
      operators.erase(key);
      updateMembers();
    } else if (p.command == "KICK" && p.args.size() >= 2) {
      users.erase(p.args[1]);
      operators.erase(casefold(p.args[1]));
      updateMembers();
    } else if (p.command == "NICK" && !p.args.empty()) {
      users.erase(nick);
      users.insert(p.args[0]);
      if (doc.characters.count(key)) {
        doc.characters[casefold(p.args[0])] = doc.characters[key];
        doc.characters.erase(key);
      }
      if (key == casefold(doc.nick))
        doc.nick = p.args[0];
      if (operators.erase(key))
        operators.insert(casefold(p.args[0]));
      updateMembers();
    } else if (p.command == "MODE" && p.args.size() >= 3 &&
               casefold(p.args[0]) == casefold(doc.channel)) {
      bool add = true;
      size_t i = 2;
      for (char c : p.args[1]) {
        if (c == '+')
          add = true;
        else if (c == '-')
          add = false;
        else if (std::string("ovhqa").find(c) != std::string::npos &&
                 i < p.args.size()) {
          auto k = casefold(p.args[i++]);
          if (c != 'v') {
            if (add)
              operators.insert(k);
            else
              operators.erase(k);
          }
        }
      }
    } else if (p.command == "PRIVMSG" && p.args.size() >= 2) {
      std::string target = p.args[0], message = p.args[1];
      bool room = casefold(target) == casefold(doc.channel);
      if (!room && casefold(target) != casefold(doc.nick))
        return;
      if (message.rfind("# Appears as ", 0) == 0) {
        auto name = casefold(message.substr(13));
        name = name.substr(0, name.find_first_of("., "));
        if (art.catalog()["characters"].contains(name))
          doc.characters[key] = name;
        else
          info(nick + " uses unavailable art: " + name);
        return;
      }
      if (message.rfind("# BDrop: ", 0) == 0 ||
          message.rfind("# BDrop2: ", 0) == 0) {
        if (room && operators.count(key)) {
          auto start = message.find(':') + 1;
          auto name = casefold(message.substr(start));
          name.erase(0, name.find_first_not_of(' '));
          name = name.substr(0, name.find_first_of("., "));
          if (art.catalog()["backdrops"].contains(name)) {
            doc.backdrop = name;
            auto it =
                std::find(backdropNames.begin(), backdropNames.end(), name);
            backdrop.value(it - backdropNames.begin());
          }
        }
        return;
      }
      Expression em = infer(message);
      message = parseAnnotations(message, em);
      if (message.size() > 8 && message[0] == '\1' &&
          message.rfind("\1"
                        "ACTION ",
                        0) == 0) {
        em.mode = 5;
        message = message.substr(8);
        if (!message.empty() && message.back() == '\1')
          message.pop_back();
      } else if (!message.empty() && message[0] == '\1') {
        if (message == "\1"
                       "VERSION\1")
          irc.send("NOTICE " + nick +
                   " :\1VERSION Comic Chat XR (DomeD FLTK)\1");
        return;
      }
      if (!room)
        em.mode = 2;
      users.insert(nick);
      doc.add({nick, message, "", em});
      sync();
    } else if (p.command == "ERROR" || p.command == "433" ||
               p.command == "403" || p.command == "473" || p.command == "474" ||
               p.command == "475" || p.command == "404")
      info(line);
  }
  fs::path chooseFile(const char *title, bool save, const char *filter) {
    Fl_Native_File_Chooser chooser;
    chooser.title(title);
    chooser.type(save ? Fl_Native_File_Chooser::BROWSE_SAVE_FILE
                      : Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.filter(filter);
    if (chooser.show() != 0)
      return {};
    return chooser.filename();
  }
  void connectDialog() {
    dx::GpuWindow dialog(520, 334, "Connect to IRC");
    Fl_Input host(118, 20, 380, 30, "Server"),
        nick(118, 60, 380, 30, "Nickname"),
        channel(118, 100, 380, 30, "Channel"),
        password(118, 220, 380, 30, "Password");
    password.type(FL_SECRET_INPUT);
    Fl_Int_Input port(118, 140, 140, 30, "Port");
    Fl_Check_Button tls(278, 140, 220, 30, "TLS");
    Fl_Box explain(20, 184, 478, 28, "TLS verifies the server certificate.");
    Fl_Button connect(280, 278, 104, 34, "Connect"),
        cancel(394, 278, 104, 34, "Cancel");
    host.value("irc.libera.chat");
    nick.value(doc.nick.c_str());
    channel.value(doc.channel.c_str());
    channel.tooltip(
        "Optional: leave empty to connect without joining a channel.");
    port.value("6697");
    tls.value(1);
    bool accepted = false;
    connect.callback(
        [](Fl_Widget *w, void *p) {
          *static_cast<bool *>(p) = true;
          w->window()->hide();
        },
        &accepted);
    cancel.callback([](Fl_Widget *w, void *) { w->window()->hide(); });
    dialog.end();
    dx::style(&dialog);
    dialog.set_modal();
    dialog.show();
    while (dialog.shown())
      Fl::wait();
    if (accepted) {
      Connection c;
      c.host = host.value();
      c.nick = nick.value();
      c.channel = channel.value();
      c.port = std::stoul(port.value());
      c.password = password.value();
      c.tls = tls.value();
      irc.connect(c);
      doc.nick = c.nick;
      doc.channel = c.channel;
      users.clear();
      operators.clear();
      registered = false;
      connectButton.label("Disconnect");
    }
  }
  void send() {
    std::string value = input.value();
    if (value.empty())
      return;
    if (!irc.connected() || !registered) {
      info("Connect to IRC before sending a message.");
      return;
    }
    if (value == "/quit") {
      irc.disconnect();
      info("Disconnected");
      return;
    }
    if (value.rfind("/nick ", 0) == 0) {
      if (!irc.send("NICK " + value.substr(6)))
        info("Nickname could not be sent");
      else
        input.value("");
      return;
    }
    if (value.rfind("/join ", 0) == 0) {
      auto channel = value.substr(6);
      if (channel.empty() || (channel[0] != '#' && channel[0] != '&') ||
          channel.find_first_of(" \r\n,:\0", 0, 6) != std::string::npos ||
          channel.size() > 100) {
        info("Use /join #channel with a valid channel name.");
        return;
      }
      if (irc.send("JOIN " + channel)) {
        doc.channel = channel;
        users.clear();
        operators.clear();
        updateMembers();
        input.value("");
      }
      return;
    }
    if (doc.channel.empty()) {
      info("Join a channel with /join #channel before sending chat messages.");
      return;
    }
    Expression e = selected;
    if (e.emotion == 0)
      e = infer(value);
    if (value.rfind("/me ", 0) == 0) {
      e.mode = 5;
      value = value.substr(4);
    } else if (value.rfind("/think ", 0) == 0) {
      e.mode = 3;
      value = value.substr(7);
    } else if (value[0] == '/') {
      info("Commands: /join, /me, /think, /nick, /quit");
      return;
    }
    Message m{doc.nick, value, doc.character, e};
    auto pose = art.pose(m.character, m.expression);
    auto wire = "PRIVMSG " + doc.channel + " :" + annotate(m, pose);
    if (!irc.send(wire)) {
      info("Message too long (IRC permits 510 bytes per line), or send queue "
           "is full.");
      return;
    }
    doc.add(m);
    sync();
    input.value("");
    input.take_focus();
  }
  void updateView() {
    if (xrView) {
      page.hide();
      text.hide();
      scene.show();
      textButton.deactivate();
    } else {
      scene.hide();
      if (useText) {
        page.hide();
        text.show();
      } else {
        text.hide();
        page.show();
      }
      textButton.activate();
    }
    xrButton.label(xrView ? "2D view" : "3D view");
    if (xrPresentation)
      xrButton.activate();
    else
      xrButton.deactivate();
    textButton.label(useText ? "Comic view" : "Text view");
    redraw();
  }
  void action(Fl_Widget *w) {
    if (w == &connectButton) {
      if (irc.connected()) {
        irc.disconnect();
        registered = false;
        connectButton.label("Connect");
        info("Disconnected");
      } else
        connectDialog();
    } else if (w == &sendButton || w == &input)
      send();
    else if (w == &character) {
      doc.character = characterNames[character.value()];
      doc.characters[casefold(doc.nick)] = doc.character;
      updatePreview();
      if (irc.connected())
        announce(doc.channel);
    } else if (w == &backdrop) {
      doc.backdrop = backdropNames[backdrop.value()];
      if (!doc.panels.empty())
        doc.panels.back().backdrop = doc.backdrop;
      sync();
      if (operators.count(casefold(doc.nick)))
        irc.send("PRIVMSG " + doc.channel + " :# BDrop2: " + doc.backdrop +
                 ".bgb,");
    } else if (w == &expression) {
      selected.emotion = expression.value();
      selected.intensity = selected.emotion == 9 ? 0 : .8;
      wheel.value(selected.emotion, selected.intensity);
      updatePreview();
    } else if (w == &xrButton) {
      enable3D = !enable3D;
      xrView = xrPresentation && enable3D;
      updateView();
    } else if (w == &textButton) {
      useText = !useText;
      updateView();
    } else if (w == &menu) {
      char path[256];
      menu.item_pathname(path, sizeof(path));
      std::string cmd = path;
      if (cmd == "File/Connect…")
        connectDialog();
      else if (cmd == "File/Disconnect") {
        irc.disconnect();
        registered = false;
        info("Disconnected");
      } else if (cmd == "File/Quit")
        hide();
      else if (cmd == "View/Comic or text")
        action(&textButton);
      else if (cmd == "View/3D view")
        action(&xrButton);
      else if (cmd == "View/Reset 3D camera")
        scene.reset();
      else if (cmd == "File/Export XR scene…") {
        auto p =
            chooseFile("Export glTF binary scene", true, "glTF binary\t*.glb");
        if (!p.empty()) {
          if (!currentPanel()) {
            info("No comic panel to export");
            return;
          }
          exportPending = p;
          requestScene();
          info("Preparing glTF export…");
        }
      } else if (cmd == "File/Save transcript…") {
        auto p =
            chooseFile("Save comic transcript", true, "Comic JSON\t*.json");
        if (!p.empty()) {
          std::ofstream out(p);
          out << doc.json().dump(2) << '\n';
          if (!out)
            throw std::runtime_error("Cannot save transcript");
          info("Transcript saved");
        }
      } else if (cmd == "File/Open transcript…") {
        auto p =
            chooseFile("Open comic transcript", false, "Comic JSON\t*.json");
        if (!p.empty()) {
          if (fs::file_size(p) > 4 * 1024 * 1024)
            throw std::runtime_error("Transcript too large");
          Json j;
          std::ifstream(p) >> j;
          Document candidate = doc;
          candidate.load(j);
          for (auto &p : candidate.panels) {
            art.backdrop(p.backdrop);
            for (auto &m : p.messages)
              art.pose(m.character, m.expression);
          }
          doc = std::move(candidate);
          sync();
        }
      } else if (cmd == "Help/About")
        fl_message(
            "Comic Chat XR\nNative FLTK port of Microsoft Comic Chat 2.5 "
            "beta-1\nOriginal Microsoft artwork and expression metadata\nMIT "
            "license — Microsoft Corporation and de-xr contributors");
    }
  }
  void tick() {
    try {
      for (auto &event : irc.receive()) {
        if (event["type"] == "line")
          process(event["line"]);
        else
          info(event.value("text", ""));
      }
      if (!irc.connected() && registered) {
        registered = false;
        connectButton.label("Connect");
      }
      if (dx::font_revision != fontRevision) {
        fontRevision = dx::font_revision;
        sync();
        updatePreview();
      }
      std::string error;
      bool ready = writer.ready(error);
      if (ready && !sceneOpened) {
        scene.open(scenePath);
        sceneOpened = true;
      }
      if (!error.empty()) {
        info(error);
      }
      auto mode = domeMode.snapshot();
      bool wasPresentation = xrPresentation;
      xrPresentation = mode.managed && mode.known && mode.xr;
      bool nextXR = xrPresentation && enable3D;
      if (nextXR != xrView) {
        xrView = nextXR;
        if (xrView)
          ++xrEntries;
        else
          ++desktopReturns;
        updateView();
      }
      if (wasPresentation != xrPresentation)
        updateView();
      scene.advance(mode);
      if (!scene.error.empty())
        info(scene.error);
      if (modeLabel != mode.message) {
        modeLabel = mode.message;
        if (mode.managed)
          info(mode.message);
      }

      if (ready && !exportPending.empty()) {
        fs::copy_file(scenePath, exportPending,
                      fs::copy_options::overwrite_existing);
        info("glTF scene exported: " + exportPending.string());
        exportPending.clear();
      }
      if (!opt.capture.empty() &&
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        started)
                  .count() > (opt.seconds > 0 ? opt.seconds : 2)) {
        Fl::flush();
        std::unique_ptr<Fl_RGB_Image> captured(
            fl_capture_window(this, 0, 0, w(), h()));
        if (!captured)
          throw std::runtime_error("Cannot capture application window");
        Image snap(captured->w(), captured->h());
        const auto *pixels =
            reinterpret_cast<const unsigned char *>(captured->data()[0]);
        int depth = captured->d(),
            stride = captured->ld() ? captured->ld() : snap.w * depth;
        for (int y = 0; y < snap.h; ++y)
          for (int x = 0; x < snap.w; ++x) {
            auto *dst = snap.rgba.data() + (size_t(y) * snap.w + x) * 4;
            auto *src = pixels + y * stride + x * depth;
            std::copy_n(src, 3, dst);
            dst[3] = 255;
          }
        snap.save(opt.capture);
        auto snapshot = doc.json();
        auto m = domeMode.snapshot();
        snapshot["presentation"] = {
            {"launch_realm", opt.resolvedRealm},
            {"managed", m.managed},
            {"known", m.known},
            {"xr", m.xr},
            {"three_d_enabled", enable3D},
            {"stereo_transport", xrPresentation && m.stereoSupported},
            {"view", xrView    ? "3d"
                     : useText ? "text"
                               : "comic"},
            {"scene_frames", scene.frames},
            {"stereo_frames", scene.stereoFrames},
            {"stereo_supported", m.stereoSupported},
            {"tracked_eyes", m.trackedEyes},
            {"curved_surface", m.hasCylinder},
            {"window_id", m.windowId},
            {"xr_entries", xrEntries},
            {"desktop_returns", desktopReturns},
            {"scene_error", scene.error},
            {"lane_height_radians", m.laneHeightRadians},
            {"content_height_m", scene.contentHeight},
            {"presenter", false}};
        std::ofstream(opt.capture.string() + ".json") << snapshot.dump(2);
        opt.capture.clear();
        hide();
      }
    } catch (const std::exception &e) {
      info(e.what());
    }
  }

public:
  void draw() override {
    if (!context_valid())
      scene.abandon();
    damage(FL_DAMAGE_ALL);
    dx::GpuWindow::draw();
    if (xrPresentation)
      scene.composite(w(), h(), pixel_w(), pixel_h(), xrView);
  }
  Window(Options options, Document document)
      : dx::GpuWindow(1120, 774, "Comic Chat XR"), opt(std::move(options)),
        art(opt.art), doc(std::move(document)), writer(opt.art) {
    char path[] = "/tmp/comic-chat-xr-XXXXXX";
    auto created = mkdtemp(path);
    if (!created)
      throw std::runtime_error("Cannot create private XR directory");
    tempDir = created;
    scenePath = tempDir / "scene.glb";
    scene.hide();
    xrButton.deactivate();
    writer.cleanupOnExit(tempDir);
    Fl_Window::default_xclass("org.de_xr.ComicChat");
    xclass("org.de_xr.ComicChat.Sbs");
    resizable(page);
    size_range(800, 680);
    text.buffer(&transcript);
    text.hide();
    members.textsize(dx::size);
    selected.emotion = 0;
    for (auto item :
         {std::pair{"File/Connect…", FL_CTRL + 'l'},
          std::pair{"File/Disconnect", 0},
          std::pair{"File/Open transcript…", FL_CTRL + 'o'},
          std::pair{"File/Save transcript…", FL_CTRL + 's'},
          std::pair{"File/Export XR scene…", 0},
          std::pair{"File/Quit", FL_CTRL + 'q'},
          std::pair{"View/Comic or text", FL_CTRL + 't'},
          std::pair{"View/3D view", 0}, std::pair{"View/Reset 3D camera", 0},
          std::pair{"Help/About", 0}})
      menu.add(item.first, item.second, callback, this);
    if (dx::dome())
      menu.hide();
    characterNames = art.characters();
    backdropNames = art.backdrops();
    for (auto &name : characterNames)
      character.add(name.c_str());
    for (auto &name : backdropNames)
      backdrop.add(name.c_str());
    character.value(
        std::find(characterNames.begin(), characterNames.end(), doc.character) -
        characterNames.begin());
    backdrop.value(
        std::find(backdropNames.begin(), backdropNames.end(), doc.backdrop) -
        backdropNames.begin());
    for (auto e : emotions)
      expression.add(e);
    expression.value(0);
    for (auto *w : std::vector<Fl_Widget *>{&connectButton, &sendButton, &input,
                                            &textButton, &xrButton, &character,
                                            &backdrop, &expression})
      w->callback(callback, this);
    input.when(FL_WHEN_ENTER_KEY_ALWAYS);
    input.tooltip("Enter sends; /me action, /think thought, /nick name, /quit");
    page.onSelect([this](int) { requestScene(); });
    wheel.onChange([this](int em, float intensity) {
      selected.emotion = em;
      selected.intensity = intensity;
      expression.value(em);
      updatePreview();
    });
    end();
    desktop = std::make_unique<DesktopMenu>(*this, menu);
    dx::style(this);
    updatePreview();
    sync();
    updateView();
    info("Disconnected — Connect to an IRC server, or open a saved comic "
         "transcript.");
    fontRevision = dx::font_revision;
    Fl::add_timeout(.05, timer, this);
  }
  ~Window() {
    if (context()) {
      make_current();
      scene.release();
    } else
      scene.abandon();
    text.buffer(nullptr);
    Fl::remove_timeout(timer, this);
    irc.disconnect();
    // The export worker owns cleanup of its private directory after it stops.
  }
  int handle(int event) override {
    // A hidden universal menu still needs the suite's local Ctrl shortcuts.
    if (event == FL_SHORTCUT && (Fl::event_state() & FL_CTRL)) {
      const char *path = nullptr;
      switch (Fl::event_key()) {
      case 'q':
        path = "File/Quit";
        break;
      case 'l':
        path = "File/Connect…";
        break;
      case 'o':
        path = "File/Open transcript…";
        break;
      case 's':
        path = "File/Save transcript…";
        break;
      case 't':
        path = "View/Comic or text";
        break;
      }
      if (path) {
        int index = menu.find_index(path);
        if (index >= 0) {
          menu.value(index);
          callback(&menu, this);
          return 1;
        }
      }
    }
    return dx::GpuWindow::handle(event);
  }
  void resize(int x, int y, int w, int h) override {
    dx::GpuWindow::resize(x, y, w, h);
    int right = w - 232, top = dx::dome() ? 42 : 74, bottom = h - 80;
    menu.resize(0, 0, w, 28);
    int ty = dx::dome() ? 6 : 34;
    connectButton.resize(8, ty, 104, 30);
    textButton.resize(120, ty, 104, 30);
    xrButton.resize(232, ty, 130, 30);
    character.resize(450, ty, 145, 30);
    backdrop.resize(700, ty, 140, 30);
    page.resize(8, top, right - 16, bottom - top);
    text.resize(8, top, right - 16, bottom - top);
    scene.resize(8, top, right - 16, bottom - top);
    members.resize(right, top, 224, std::max(100, (bottom - top) / 3));
    preview.resize(right, top + (bottom - top) / 3 + 8, 224,
                   std::max(100, (bottom - top) / 3 - 8));
    wheel.resize(right, bottom - 206, 224, 170);
    expression.resize(right + 72, bottom - 28, 152, 28);
    input.resize(8, h - 70, right - 16, 34);
    sendButton.resize(right, h - 70, 224, 34);
    status.resize(8, h - 28, w - 16, 24);
  }
};
int ircProbe(const Options &o) {
  Irc irc;
  Connection c;
  c.host = o.host;
  c.port = o.port;
  c.tls = o.tls;
  c.nick = o.nick;
  c.channel = o.channel;
  irc.connect(c);
  Json events = Json::array();
  bool sent = false;
  auto until = std::chrono::steady_clock::now() +
               std::chrono::duration<double>(o.seconds > 0 ? o.seconds : 5);
  while (std::chrono::steady_clock::now() < until) {
    for (auto &event : irc.receive()) {
      events.push_back(event);
      if (event.value("type", "") == "line") {
        auto line = parseIrc(event["line"]);
        if (line.command == "001" && !sent) {
          sent = true;
          irc.send(c.channel.empty()
                       ? "WHOIS " + c.nick
                       : "PRIVMSG " + c.channel + " :probe roundtrip");
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  irc.disconnect();
  if (!o.probeOutput.empty())
    std::ofstream(o.probeOutput) << events.dump(2);
  else
    std::cout << events.dump(2) << '\n';
  return sent ? 0 : 1;
}
} // namespace
int main(int argc, char **argv) {
  try {
    auto o = options(argc, argv);
    if (!o.xr.empty()) {
      auto parent = getppid();
      prctl(PR_SET_PDEATHSIG, SIGTERM);
      if (getppid() != parent)
        return 1;
      return fsv::gltf::runXr(o.xr, o.seconds, o.report);
    }
    if (o.xrProbe) {
      auto p = fsv::gltf::probeXr();
      std::cout << Json{{"available", p.available},
                        {"runtime", p.runtime},
                        {"message", p.message}}
                       .dump()
                << '\n';
      return p.available ? 0 : 1;
    }
    if (o.ircProbe)
      return ircProbe(o);
    if (o.sessionStatus) {
      auto realm = configureLaunchRealm(o.realm);
      DomeMonitor monitor;
      DomeMode mode;
      for (int i = 0; i < 150; ++i) {
        mode = monitor.snapshot();
        if (mode.known)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      std::cout << Json{{"launch_realm", realm},
                        {"wayland_display", getenv("WAYLAND_DISPLAY")
                                                ? getenv("WAYLAND_DISPLAY")
                                                : ""},
                        {"managed", mode.managed},
                        {"known", mode.known},
                        {"pullout_depth_m", mode.pulloutDistance},
                        {"lane_height_radians", mode.laneHeightRadians},
                        {"stereo_supported", mode.stereoSupported},
                        {"shared_pullout_anchor", mode.hasPulloutAnchor},
                        {"message", mode.message},
                        {"xr", mode.xr}}
                       .dump()
                << '\n';
      return 0;
    }
    Art art(o.art);
    if (!o.exportLibrary.empty()) {
      exportAssets(art, o.exportLibrary);
      std::cout
          << "Exported 31 depth cutouts and 7 layered parallax backdrops\n";
      return 0;
    }
    if (!o.exportOne.empty()) {
      Expression e;
      e.emotion = o.emotion;
      e.intensity = o.intensity;
      exportCharacter(o.characterName, e, art, o.exportOne);
      return 0;
    }
    Document doc;
    if (o.demo)
      doc.demo();
    if (!o.transcript.empty()) {
      if (fs::file_size(o.transcript) > 4 * 1024 * 1024)
        throw std::runtime_error("Transcript too large");
      Json j;
      std::ifstream(o.transcript) >> j;
      doc.load(j);
    }
    if (!o.render.empty() || !o.exportPath.empty() || !o.sceneCapture.empty()) {
      if (doc.panels.empty())
        throw std::runtime_error(
            "Use --demo or --transcript to render a comic");
      if (!o.render.empty())
        render(doc.panels.back(), art).panel.save(o.render);
      auto path = o.exportPath;
      if (!o.sceneCapture.empty() && path.empty())
        path = o.sceneCapture.string() + ".glb";
      if (!path.empty())
        exportScene(doc.panels.back(), art, path);
      if (!o.sceneCapture.empty())
        fsv::gltf::renderModel(path, o.sceneCapture, 0);
      std::cout << Json{{"characters", art.characters().size()},
                        {"backdrops", art.backdrops().size()},
                        {"panels", doc.panels.size()},
                        {"glb", path.string()}}
                       .dump()
                << '\n';
      return 0;
    }
    o.resolvedRealm = configureLaunchRealm(o.realm);
    Fl::lock();
    dx::init();
    Window window(o, doc);
    window.show();
    window.resize(window.x(), window.y(), window.w(), window.h());
    return Fl::run();
  } catch (const std::exception &e) {
    std::cerr << "Comic Chat XR: " << e.what() << '\n';
    return 1;
  }
}
