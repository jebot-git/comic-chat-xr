// SPDX-License-Identifier: MIT
#pragma once
#include "domed.h"
#include "object-math.h"
namespace comic {
// Portable GLBs retain their canonical 2 m reference plane. Only the renderer's
// root transform aligns that plane with DomeD, preserving physical mesh depth
// and all relative distances between the backdrop layers.
class ScenePlacement {
  fsv::gltf::Pose anchor;
  bool captured = false;

public:
  void reset() { captured = false; }
  fsv::gltf::Mat4 root(const DomeMode &mode, fsv::gltf::Pose head) {
    using namespace fsv::gltf;
    if (!mode.managed)
      return {};
    if (mode.hasPulloutAnchor) {
      anchor = {{mode.pulloutPosition[0], mode.pulloutPosition[1],
                 mode.pulloutPosition[2]},
                {mode.pulloutOrientation[0], mode.pulloutOrientation[1],
                 mode.pulloutOrientation[2], mode.pulloutOrientation[3]},
                1};
      captured = true;
    } else if (!captured) {
      auto forward = rotate(head.orientation, {0, 0, -1});
      float yaw = std::atan2(-forward.x, -forward.z);
      anchor = {head.position, {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, 1};
      captured = true;
    }
    // DomeD centres pulled-out windows 6 cm below its upright opening anchor.
    fsv::gltf::Pose offset{
        {0, -1.6f - .06f, 2.f - mode.pulloutDistance}, {}, 1};
    return matrix(anchor) * matrix(offset);
  }
};
} // namespace comic
