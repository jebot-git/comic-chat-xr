// SPDX-License-Identifier: MIT
#include "comic.h"
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <pango/pangocairo.h>
#include <stdexcept>
namespace comic {
namespace {
struct Surface {
  std::vector<unsigned char> pixels;
  int w, h;
  cairo_surface_t *surface = nullptr;
  cairo_t *cr = nullptr;
  Surface(int width, int height)
      : pixels(size_t(width) * height * 4), w(width), h(height) {
    surface = cairo_image_surface_create_for_data(
        pixels.data(), CAIRO_FORMAT_ARGB32, w, h, w * 4);
    cr = cairo_create(surface);
  }
  ~Surface() {
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
  }
  Image image() {
    cairo_surface_flush(surface);
    Image out(w, h);
    for (size_t i = 0; i < pixels.size(); i += 4) {
      unsigned a = pixels[i + 3];
      out.rgba[i] = a ? std::min(255u, pixels[i + 2] * 255u / a) : 255;
      out.rgba[i + 1] = a ? std::min(255u, pixels[i + 1] * 255u / a) : 255;
      out.rgba[i + 2] = a ? std::min(255u, pixels[i] * 255u / a) : 255;
      out.rgba[i + 3] = a;
    }
    return out;
  }
};
void paint(cairo_t *cr, const Image &im, double x, double y, double w,
           double h) {
  Surface tmp(im.w, im.h);
  for (size_t i = 0; i < im.rgba.size(); i += 4) {
    auto a = im.rgba[i + 3];
    tmp.pixels[i] = im.rgba[i + 2] * a / 255;
    tmp.pixels[i + 1] = im.rgba[i + 1] * a / 255;
    tmp.pixels[i + 2] = im.rgba[i] * a / 255;
    tmp.pixels[i + 3] = a;
  }
  cairo_surface_mark_dirty(tmp.surface);
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, w / im.w, h / im.h);
  cairo_set_source_surface(cr, tmp.surface, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
  cairo_rectangle(cr, 0, 0, im.w, im.h);
  cairo_fill(cr);
  cairo_restore(cr);
}
void ellipse(cairo_t *cr, double x, double y, double w, double h) {
  cairo_new_sub_path(cr);
  cairo_save(cr);
  cairo_translate(cr, x + w / 2, y + h / 2);
  cairo_scale(cr, w / 2, h / 2);
  cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
  cairo_restore(cr);
}
// CBeta tension=5, bias=1 from Microsoft's spline.cpp. These are the
// original cubic-to-Bezier coefficients, applied to the balloon's contour.
void woodring(cairo_t *cr, const std::vector<std::array<double, 2>> &points) {
  const double m[4][4] = {{-2. / 17, 16. / 17, -16. / 17, 2. / 17},
                          {6. / 17, -27. / 17, 21. / 17, 0},
                          {-6. / 17, 0, 6. / 17, 0},
                          {2. / 17, 13. / 17, 2. / 17, 0}};
  for (size_t i = 0; i < points.size(); ++i) {
    double c[4][2]{};
    for (int row = 0; row < 4; ++row)
      for (int axis = 0; axis < 2; ++axis)
        for (int k = 0; k < 4; ++k)
          c[row][axis] +=
              m[row][k] *
              points[(i + k + points.size() - 1) % points.size()][axis];
    double x = c[3][0], y = c[3][1], x1 = x + c[2][0] / 3, y1 = y + c[2][1] / 3;
    if (!i)
      cairo_move_to(cr, x, y);
    cairo_curve_to(
        cr, x1, y1, x1 + (c[2][0] + c[1][0]) / 3, y1 + (c[2][1] + c[1][1]) / 3,
        x + c[2][0] + c[1][0] + c[0][0], y + c[2][1] + c[1][1] + c[0][1]);
  }
  cairo_close_path(cr);
}
void bubble(cairo_t *cr, double x, double y, double w, double h, int mode,
            double tailX, double tailY) {
  cairo_new_path(cr);
  cairo_set_line_width(cr, 2);
  cairo_set_source_rgb(cr, 1, 1, 1);
  if (mode == 4) {
    for (int i = 0; i < 32; ++i) {
      double angle = i * 2 * M_PI / 32;
      double scale = i % 2 ? .88 : 1.06;
      double px = x + w / 2 + std::cos(angle) * w * .5 * scale,
             py = y + h / 2 + std::sin(angle) * h * .5 * scale;
      if (i)
        cairo_line_to(cr, px, py);
      else
        cairo_move_to(cr, px, py);
    }
    cairo_close_path(cr);
  } else if (mode == 3) {
    std::vector<std::array<double, 2>> points;
    for (int i = 0; i < 48; ++i) {
      double a = i * 2 * M_PI / 48, r = i % 4 == 0 ? 1.08 : .94;
      points.push_back({x + w / 2 + std::cos(a) * w / 2 * r,
                        y + h / 2 + std::sin(a) * h / 2 * r});
    }
    woodring(cr, points);
  } else {
    double r = std::min(20., h * .3);
    woodring(cr, {{x + r, y},
                  {x + w / 2, y - 2},
                  {x + w - r, y},
                  {x + w, y + r},
                  {x + w, y + h - r},
                  {x + w - r, y + h},
                  {x + w / 2, y + h + 2},
                  {x + r, y + h},
                  {x, y + h - r},
                  {x, y + r}});
  }
  if (mode == 2) {
    double dash[] = {4, 3};
    cairo_set_dash(cr, dash, 2, 0);
  }
  cairo_fill_preserve(cr);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_stroke(cr);
  cairo_set_dash(cr, nullptr, 0, 0);
  if (mode == 3) {
    for (int i = 0; i < 3; ++i) {
      double t = (i + 1) / 4.;
      double cx = (x + w / 2) * (1 - t) + tailX * t,
             cy = (y + h * .85) * (1 - t) + tailY * t;
      ellipse(cr, cx - 5 + i, cy - 5 + i, 10 - 2 * i, 10 - 2 * i);
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_stroke(cr);
    }
  } else if (mode != 5) {
    double base = std::clamp(tailX, x + w * .3, x + w * .7);
    cairo_move_to(cr, base - 8, y + h - 10);
    cairo_line_to(cr, tailX, tailY);
    cairo_line_to(cr, base + 9, y + h - 12);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);
  }
}
void text(cairo_t *cr, const std::string &value, const std::string &font,
          int size, int x, int y, int w, int h) {
  auto *layout = pango_cairo_create_layout(cr);
  auto *desc = pango_font_description_from_string(font.c_str());
  pango_layout_set_text(layout, value.c_str(), value.size());
  pango_layout_set_width(layout, w * PANGO_SCALE);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
  int tw = 0, th = 0;
  do {
    pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
    pango_layout_set_font_description(layout, desc);
    pango_layout_get_pixel_size(layout, &tw, &th);
    if (th <= h || size <= 9)
      break;
    --size;
  } while (true);
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  cairo_move_to(cr, x, y + std::max(0, (h - th) / 2));
  cairo_set_source_rgb(cr, 0, 0, 0);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(desc);
  g_object_unref(layout);
}
} // namespace
BalloonMetrics measureBalloon(const std::string &value, const std::string &font,
                              int size) {
  Surface surface(1, 1);
  auto *layout = pango_cairo_create_layout(surface.cr);
  auto *desc = pango_font_description_from_string(font.c_str());
  pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
  pango_layout_set_font_description(layout, desc);
  char *upper = g_utf8_strup(value.c_str(), value.size());
  pango_layout_set_text(layout, upper, -1);
  g_free(upper);
  int width = 0, height = 0;
  pango_layout_get_pixel_size(layout, &width, &height);
  double goal =
      std::clamp(std::sqrt(std::max(1, width) * std::max(1, height) * 2.1),
                 280., double(PANEL - 48));
  pango_layout_set_width(layout, int(goal - 36) * PANGO_SCALE);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  pango_layout_get_pixel_size(layout, &width, &height);
  pango_font_description_free(desc);
  g_object_unref(layout);
  return {goal, double(std::max(42, height + 28))};
}
bool balloonsFit(const Panel &panel, const std::string &font, int size) {
  double used = 9;
  for (auto &message : panel.messages) {
    auto value = message.expression.mode == 5
                     ? message.nick + " " + message.text
                     : message.text;
    used += measureBalloon(value, font, size).height + 6;
  }
  return used <= PANEL * .47 && panel.messages.size() <= 10;
}
Rendered render(const Panel &panel, Art &art, const std::string &font,
                int fontSize) {
  Rendered out;
  out.layout = layout(panel, art);
  Surface image(PANEL, PANEL), balloons(PANEL, PANEL);
  auto backdrop = art.backdrop(panel.backdrop);
  paint(image.cr, *backdrop, 0, 0, PANEL, PANEL);
  for (auto &a : out.layout.actors)
    paint(image.cr, *a.pose.image, a.x, a.y, a.w, a.h);
  size_t count = panel.messages.size();
  double nextY = 9;
  for (size_t i = 0; i < count; ++i) {
    auto &m = panel.messages[i];
    auto actor = std::find_if(
        out.layout.actors.begin(), out.layout.actors.end(),
        [&](auto &a) { return casefold(a.nick) == casefold(m.nick); });
    auto value = m.expression.mode == 5 ? m.nick + " " + m.text : m.text;
    auto metrics = measureBalloon(value, font, fontSize);
    double w = metrics.width, h = metrics.height;
    double centre =
        actor == out.layout.actors.end() ? PANEL / 2 : actor->x + actor->w / 2;
    double x = std::clamp(centre - w / 2, 12., PANEL - w - 12), y = nextY;
    nextY += h + 6;
    double tailX =
        actor == out.layout.actors.end()
            ? PANEL / 2
            : actor->x + actor->w * actor->pose.faceX / actor->pose.image->w;
    double tailY = actor == out.layout.actors.end()
                       ? PANEL * .58
                       : actor->y + actor->h * actor->pose.headHeight /
                                        actor->pose.image->h * .6;
    int mode = m.expression.mode == 1 && m.expression.emotion == 7
                   ? 4
                   : m.expression.mode;
    bubble(balloons.cr, x, y, w, h, mode, tailX, tailY);
    // Original CBWoodringNormal uppercases balloon text; the avatar identifies
    // its speaker. The text-only view retains nicknames and original casing.
    char *upper = g_utf8_strup(value.c_str(), value.size());
    text(balloons.cr, upper, font, fontSize, x + 18, y + 14, w - 36, h - 28);
    g_free(upper);
  }
  out.balloons = balloons.image();
  paint(image.cr, out.balloons, 0, 0, PANEL, PANEL);
  cairo_rectangle(image.cr, 1, 1, PANEL - 2, PANEL - 2);
  cairo_set_line_width(image.cr, 3);
  cairo_set_source_rgb(image.cr, 0, 0, 0);
  cairo_stroke(image.cr);
  out.panel = image.image();
  return out;
}
} // namespace comic
