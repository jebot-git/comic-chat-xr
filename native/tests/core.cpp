// SPDX-License-Identifier: MIT
#include "comic.h"
#include "chat-policy.h"
#include "gltf-model.h"
#include "irc-output.h"
#include "irc-command.h"
#include "scene-placement.h"
#include "window-stereo.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
int main(int argc, char **argv) {
  using namespace comic;
  assert(argc == 3);
  assert(automaticBackdrop("#SPACE", {"room", "space", "field"}) == "space");
  assert(automaticBackdrop("#unknown", {"room", "space"}) == automaticBackdrop("#UNKNOWN", {"room", "space"}));
  Message plainMessage{"A", "Message without metadata", "anna", {}};
  assert(messagePayload(plainMessage, {}, false) == plainMessage.text);
  plainMessage.expression.mode = 3;
  assert(messagePayload(plainMessage, {}, false) == "\1" "ACTION thinks Message without metadata\1");
  plainMessage.expression.mode = 5;
  assert(messagePayload(plainMessage, {}, false) == "\1" "ACTION Message without metadata\1");
  assert(messagePayload(plainMessage, {}, true).rfind("(#G", 0) == 0);

  assert(inputCommand("//literal").args == "/literal");
  assert(inputCommand("/WHOIS   Anna").name == "whois");
  assert(inputCommand("/WHOIS   Anna").args == "Anna");
  assert(inputCommand("/away").args.empty());
  assert(protocolCommand("quote") == "RAW");
  assert(protocolCommand("leave") == "PART");
  assert(protocolCommand("unknown").empty());
  assert(ircToken("#世界"));
  assert(!ircToken("nick :injection"));
  assert(!ircToken(std::string("nick\0bad", 8)));

  {
    using namespace fsv::gltf;
    CylinderSurface cylinder;
    cylinder.horizontal = 1.2f;
    auto centre = cylinder.tangent(.3f, .7f);
    assert(length(transform(inverse(centre), cylinder.point(.3f, .7f))) < 1e-5);
    auto a = transform(inverse(centre), cylinder.point(.31f, .7f));
    assert(a.x > 0 && std::abs(a.y) < 1e-5 && a.z > 0);
    Vec3 left{-.032f, .025f, .1f}, right{.032f, .025f, .1f};
    auto l = windowEyeProjection(left, 1.2f, .8f, 2);
    auto r = windowEyeProjection(right, 1.2f, .8f, 2);
    for (auto p : {Vec3{0, 1.6f, -2}, Vec3{.2f, 1.7f, -2}}) {
      assert(length(point(l, p) - point(r, p)) < 1e-5f);
    }
    auto nearL = point(l, {0, 1.6f, -1.8f}), nearR = point(r, {0, 1.6f, -1.8f});
    auto farL = point(l, {0, 1.6f, -4}), farR = point(r, {0, 1.6f, -4});
    assert(nearL.x > nearR.x && farL.x < farR.x);
    assert(std::abs(farL.y - farR.y) < 1e-5f);
    bool rejected = false;
    try {
      windowEyeProjection({0, 0, -2}, 1, 1, 2);
    } catch (...) {
      rejected = true;
    }
    assert(rejected);
  }
  assert(ircOutput(parseIrc(":server 001 Guest :Welcome")) ==
         "[server] 001 Welcome");
  assert(ircOutput(parseIrc(":server 372 Guest :Message of the day")) ==
         "[server] 372 Message of the day");
  assert(ircOutput(parseIrc(":server NOTICE AUTH :Checking your hostname")) ==
         "[server] -server- Checking your hostname");
  assert(ircOutput(parseIrc(":Anna!a@host NOTICE #room :Channel notice")) ==
         "[#room] -Anna- Channel notice");
  assert(ircOutput(parseIrc(":Anna!a@host JOIN :#room")) ==
         "[#room] * Anna joined");
  assert(ircOutput(parseIrc(":Anna!a@host PART #room :Leaving")) ==
         "[#room] * Anna left (Leaving)");
  assert(ircOutput(parseIrc(":Anna!a@host TOPIC #room :Topic text")) ==
         "[#room] * Anna set topic: Topic text");
  assert(
      ircOutput(parseIrc(":Anna!a@host PRIVMSG #room :(#G099E119RM1) Hello")) ==
      "[#room] Anna: Hello");
  assert(ircOutput(parseIrc(":Anna!a@host PRIVMSG #room :\1"
                            "ACTION waves\1")) == "[#room] * Anna waves");
  assert(ircOutput(parseIrc("PING :token")).empty());
  assert(ircOutput(parseIrc(":Anna!a@host PRIVMSG #room :# Appears as anna"))
             .empty());
  Art art(argv[1]);
  fs::path qa = argv[2];
  fs::create_directories(qa);
  assert(art.characters().size() == 31);
  assert(art.backdrops().size() == 7);
  size_t poses = 0;
  for (auto &name : art.characters())
    for (int e = 1; e <= 17; ++e)
      for (float intensity : {0.f, .5f, 1.f}) {
        Expression expression;
        expression.emotion = e;
        expression.intensity = intensity;
        auto p = art.pose(name, expression),
             flip = art.pose(name, expression, true);
        assert(p.image->w > 0 && p.image->h > 0);
        assert(p.image->rgba.size() == size_t(p.image->w) * p.image->h * 4);
        assert(flip.image->w == p.image->w);
        bool opaque = false, transparent = false;
        for (size_t i = 3; i < p.image->rgba.size(); i += 4) {
          opaque |= p.image->rgba[i] == 255;
          transparent |= p.image->rgba[i] == 0;
        }
        assert(opaque);
        assert(transparent);
        ++poses;
      }
  for (auto &name : art.backdrops()) {
    auto b = art.backdrop(name);
    assert(b->w > 0 && b->h > 0);
  }
  Expression e;
  e.emotion = 8;
  e.intensity = .8;
  Message m{"Anna", "Hello, 世界 👋", "anna", e};
  auto pose = art.pose("anna", e);
  auto wire = annotate(m, pose);
  Expression parsed;
  auto text = parseAnnotations(wire, parsed);
  assert(text == m.text && parsed.cooked && parsed.face == pose.face &&
         parsed.torso == pose.torso);
  assert(art.pose("anna", parsed).image->rgba == pose.image->rgba);
  for (auto bad : {"(#G) test", "(#G000EzzzRM1) invalid",
                   "(#G000E111RM9) invalid", "(#Unknown) message"}) {
    Expression x;
    assert(parseAnnotations(bad, x) == bad && !x.cooked);
  }
  auto ping = parseIrc("PING :one two");
  assert(ping.command == "PING" && ping.args.size() == 1 &&
         ping.args[0] == "one two");
  auto irc = parseIrc("@time=x :Anna!a@host PRIVMSG #room :Hello there");
  assert(irc.prefix == "Anna!a@host" && irc.args[1] == "Hello there");
  assert(casefold("NICK[\\^") == "nick{|~");
  assert(plain("a\r\nb") == "ab");
  exportScene(Panel{{}, "room"}, art, qa / "idle-background.glb");
  auto idle = fsv::gltf::load(qa / "idle-background.glb");
  assert(idle->evaluate(0).draws.size() == 3);
  assert(idle->images.size() == 3);
  Document doc;
  doc.demo();
  assert(doc.panels.size() == 2);
  Document emptyRoom;
  emptyRoom.panels.push_back({{}, "space"});
  Document restoredRoom; restoredRoom.load(emptyRoom.json());
  assert(restoredRoom.json() == emptyRoom.json() && restoredRoom.backdrop == "space");
  auto serialized = doc.json();
  Document other;
  other.load(serialized);
  assert(other.json() == serialized);
  auto panel = render(doc.panels.back(), art);
  assert(panel.layout.actors.size() == 2);
  assert(panel.layout.actors[0].x < panel.layout.actors[1].x);
  panel.panel.save(qa / "comic-panel.png");
  auto verifyTailClearance = [](const Rendered &rendered) {
    int scale = rendered.tails.w / PANEL;
    for (const auto &actor : rendered.layout.actors)
      for (int y = std::max(0, int(actor.y));
           y < std::min(PANEL, int(actor.y + actor.h)); ++y)
        for (int x = std::max(0, int(actor.x));
             x < std::min(PANEL, int(actor.x + actor.w)); ++x) {
          int sx = std::clamp(
              int((x + .5 - actor.x) / actor.w * actor.pose.image->w), 0,
              actor.pose.image->w - 1);
          int sy = std::clamp(
              int((y + .5 - actor.y) / actor.h * actor.pose.image->h), 0,
              actor.pose.image->h - 1);
          if (actor.pose.image
                  ->rgba[(size_t(sy) * actor.pose.image->w + sx) * 4 + 3] < 128)
            continue;
          for (int dy = 0; dy < scale; ++dy)
            for (int dx = 0; dx < scale; ++dx)
              assert(rendered.tails
                         .rgba[(size_t(y * scale + dy) * rendered.tails.w +
                                x * scale + dx) *
                                   4 +
                               3] == 0);
        }
  };
  verifyTailClearance(panel);
  // Normal, whisper, thought, shout and action modes retain their own routing;
  // mirrored participants must not have a tail drawn through either silhouette.
  auto alphaBounds = [](const Image &image) {
    std::array<int, 4> bounds{image.w, image.h, -1, -1};
    for (int y = 0; y < image.h; ++y)
      for (int x = 0; x < image.w; ++x)
        if (image.rgba[(size_t(y) * image.w + x) * 4 + 3] >= 128) {
          bounds[0] = std::min(bounds[0], x);
          bounds[1] = std::min(bounds[1], y);
          bounds[2] = std::max(bounds[2], x);
          bounds[3] = std::max(bounds[3], y);
        }
    return bounds;
  };
  std::set<std::vector<unsigned char>> balloonStyles;
  for (int mode : {1, 2, 3, 4, 5}) {
    auto p = doc.panels.back();
    for (auto &m : p.messages)
      m.expression.mode = mode;
    auto routed = render(p, art, "monospace", 24, 3);
    verifyTailClearance(routed);
    auto desktopStyle = render(p, art, "monospace", 24, 1);
    auto low = alphaBounds(desktopStyle.balloons),
         high = alphaBounds(routed.balloons);
    for (int axis = 0; axis < 4; ++axis) {
      if (std::abs(low[axis] - high[axis] / 3.) >= (mode == 4 ? 3 : 2))
        std::cerr << "Balloon scale bounds mode=" << mode << " axis=" << axis
                  << " desktop=" << low[axis] << " xr=" << high[axis] / 3.
                  << "\n";
      assert(std::abs(low[axis] - high[axis] / 3.) < (mode == 4 ? 3 : 2));
    }
    balloonStyles.insert(desktopStyle.balloons.rgba);

    size_t visible = 0;
    for (size_t i = 3; i < routed.tails.rgba.size(); i += 4)
      visible += routed.tails.rgba[i] != 0;
    assert(mode == 5 ? visible == 0 : visible > 10);
    routed.panel.save(qa / ("tail-mode-" + std::to_string(mode) + ".png"));
  }
  assert(balloonStyles.size() == 5);
  Panel curvePanel{{Message{"Anna", "Curved tail", "anna", {}}}, "room"};
  auto curved = render(curvePanel, art);
  auto centerAt = [&](int y) {
    double sum = 0, count = 0;
    for (int x = 0; x < PANEL; ++x)
      if (curved.tails.rgba[(y * PANEL + x) * 4 + 3] >= 128) {
        sum += x;
        ++count;
      }
    assert(count > 0);
    return sum/count;
  };
  assert(std::abs(centerAt(130) - (centerAt(80)+centerAt(180))/2) > .5);

  exportScene(doc.panels.back(), art, qa / "scene.glb");
  auto gltf = fsv::gltf::load(qa / "scene.glb");
  assert(gltf->images.size() == 6);
  // Artwork remains pixel-sharp while text has enough source detail for
  // oblique stereo sampling, with a separate mipmapped linear sampler.
  assert(gltf->images.back().width == PANEL * 3);
  assert(gltf->images.back().height == PANEL * 3);
  assert(gltf->textures.back().mag == 9729 && gltf->textures.back().min == 9987);
  for (size_t i = 0; i + 1 < gltf->textures.size(); ++i)
    assert(gltf->textures[i].mag == 9728 && gltf->textures[i].min == 9728);
  auto sceneMetadata = [&](const fs::path &path) {
    std::ifstream stream(path, std::ios::binary);
    stream.seekg(12);
    uint32_t length = 0;
    stream.read(reinterpret_cast<char *>(&length), 4);
    stream.seekg(20);
    std::string json(length, ' ');
    stream.read(json.data(), length);
    return Json::parse(json);
  };
  Panel shortMessage{{Message{"Anna", "Readable in stereo", "anna", {}}}, "room"};
  exportScene(shortMessage, art, qa / "readable-text.glb", "monospace", 14);
  auto textMetadata = sceneMetadata(qa / "readable-text.glb")["nodes"].back()["extras"];
  assert(textMetadata["fontSize"] == 24);
  assert(balloonsFit(shortMessage, "monospace", textMetadata["fontSize"]));
  auto denseMetadata = sceneMetadata(qa / "scene.glb")["nodes"].back()["extras"];
  assert(int(denseMetadata["fontSize"]) >= 18);
  assert(std::abs(float(denseMetadata["referenceDepthMetres"]) - 1.94f) < 1e-5);
  assert(balloonsFit(doc.panels.back(), "monospace", denseMetadata["fontSize"]));

  auto frame = gltf->evaluate(0);
  assert(frame.draws.size() == 6);
  assert(std::abs(frame.draws[0].world[14] + 7.f) < .00001);
  assert(std::abs(frame.draws[1].world[14] + 4.6f) < .00001);
  assert(std::abs(frame.draws[2].world[14] + 3.2f) < .00001);
  assert(frame.draws[0].primitive->vertices.size() == 4 &&
         frame.draws[2].primitive->vertices.size() == 4);
  float zmin = 1e20, zmax = -1e20;
  size_t vertices = 0;
  for (auto &v : frame.draws[3].primitive->vertices) {
    zmin = std::min(zmin, v.position.z);
    zmax = std::max(zmax, v.position.z);
    if (std::abs(v.normal.z) < .5f) {
      auto &image = gltf->images[3];
      int x = std::clamp(int(v.uv[0] * image.width), 0, image.width - 1);
      int y = std::clamp(int(v.uv[1] * image.height), 0, image.height - 1);
      assert(image.rgba[(size_t(y) * image.width + x) * 4 + 3] >= 128);
    }
    ++vertices;
  }
  assert(std::abs(zmax - zmin - .035f) < .00001);
  assert(vertices > 100);
  exportCharacter("anna", e, art, qa / "anna.glb");
  auto standalone = fsv::gltf::load(qa / "anna.glb");
  assert(standalone->evaluate(0).draws.size() == 1);
  assert(std::abs((standalone->bounds.max.y - standalone->bounds.min.y) -
                  1.65f) < .00001);
  assert(infer(">:(").emotion == 6);
  auto names = art.characters();
  for (auto &name : names) {
    auto p = art.pose(name, e);
    Message m{"Test", "Original wire state", name, e};
    Expression decoded;
    parseAnnotations(annotate(m, p), decoded);
    assert(art.pose(name, decoded).image->rgba == p.image->rgba);
  }
  // The portable reference plane follows the same opening pose and distance
  // as DomeD's pulled-out windows, without scaling mesh thickness or parallax.
  {
    using namespace fsv::gltf;
    ScenePlacement placement;
    DomeMode mode;
    mode.managed = mode.known = mode.xr = true;
    mode.pulloutDistance = 1.2f;
    fsv::gltf::Pose head{
        {1, 1.8f, 3}, {0, std::sqrt(.5f), 0, std::sqrt(.5f)}, 1};
    auto root = placement.root(mode, head);
    auto centre = point(root, {0, 1.6f, -2});
    assert(length(centre - Vec3{-.2f, 1.74f, 3}) < .00001);
    assert(placement.root(mode, fsv::gltf::Pose{{9, 9, 9}, {}, 1}).v == root.v);
    mode.pulloutDistance = 3;
    centre = point(placement.root(mode, head), {0, 1.6f, -2});
    assert(length(centre - Vec3{-2, 1.74f, 3}) < .00001);
    mode.hasPulloutAnchor = true;
    mode.pulloutPosition = {-.5f, 1.65f, .75f};
    mode.pulloutOrientation = {0, 1, 0, 0};
    mode.pulloutDistance = .9f;
    root = placement.root(mode, head);
    centre = point(root, {0, 1.6f, -2});
    assert(length(centre - Vec3{-.5f, 1.59f, 1.65f}) < .00001);
    assert(std::abs(length(vector(root, {0, 0, .035f})) - .035f) < .00001);
    assert(std::abs(length(point(root, {0, 1.6f, -3.2f}) - centre) - 1.2f) <
           .00001);
  }
  // A repeated speaker's expression must not disappear inside a single actor.
  Document changing;
  changing.add({"A", "Happy", "anna", infer(":)")});
  changing.add({"A", "Sad", "anna", infer(":(")});
  assert(changing.panels.size() == 2);
  // Entire UTF-8 messages survive balloon pagination and transcript round
  // trips.
  Document longComic;
  std::string original;
  for (int i = 0; i < 120; ++i)
    original += "Hello 世界 👋 ";
  longComic.add({"Unicode", original, "anna", {}});
  std::string reconstructed;
  for (auto &p : longComic.panels) {
    assert(balloonsFit(p, longComic.font, longComic.fontSize));
    for (auto &m : p.messages)
      reconstructed += m.text;
  }
  assert(reconstructed == original && longComic.panels.size() > 1);
  Document reloaded;
  reloaded.load(longComic.json());
  std::ofstream(qa / "long-before.json") << longComic.json().dump(2);
  std::ofstream(qa / "long-after.json") << reloaded.json().dump(2);
  assert(reloaded.json() == longComic.json());
  auto before = doc.json();
  auto malformed = before;
  malformed["panels"][1]["messages"][0]["text"] = Json::array();
  try {
    doc.load(malformed);
    assert(false);
  } catch (const Json::exception &) {
  }
  assert(doc.json() == before);
  // All layers reconstruct every original source pixel exactly at the reference
  // eye.
  for (auto &name : art.backdrops()) {
    auto original = art.backdrop(name);
    Image combined(original->w, original->h);
    auto &layers = art.catalog()["backdrops"][name]["layers"];
    assert(layers.size() == 3);
    float previous = 100;
    for (auto &layer : layers) {
      float depth = layer["depth"];
      assert(depth < previous && depth > 2);
      previous = depth;
      combined.over(*art.image(layer["image"]), 0, 0);
    }
    assert(combined.rgba == original->rgba);
  }
  // All expression states can become valid glTF, including gestures and
  // mirrored bodies.
  for (int e : {1, 5, 6, 8, 9, 10, 14, 17}) {
    Panel p = {{{"A", "Expression " + std::to_string(e), "anna", Expression{}}},
               "space"};
    p.messages[0].expression.emotion = e;
    p.messages[0].expression.intensity = 1;
    exportScene(p, art, qa / "expressions.glb");
    auto loaded = fsv::gltf::load(qa / "expressions.glb");
    assert(loaded->evaluate(0).draws.size() == 5);
  }
  std::cout << "PASS " << poses
            << " pose selections; 31 original characters; 7 original "
               "backdrops; annotations; layout; GLB geometry; expressions\n";
}
