// TrailRenderer.h — Ribbon trails (sword swipes, projectile vapor, dash slipstream).
// Algorithm: ring buffer of waypoints; consecutive waypoints become quad strips with UV.t
// scrolling for animated textures. Width tapers from head (1.0) to tail (0.0) for the wisp
// look. Used heavily for melee attacks and Fortnite-style damage trails.
#pragma once
#include "Core/Math/Vector.h"
#include <string>
#include <vector>

namespace dionite::vfx {
using math::Vec3;
using math::Vec4;

struct TrailPoint {
    Vec3  position;
    float born  = 0.f;
    Vec4  color {1,1,1,1};
};

struct TrailRibbon {
    std::string texture;
    int         maxPoints     = 48;
    float       pointLifetime = 0.35f;
    float       widthHead     = 0.22f;
    float       widthTail     = 0.02f;
    Vec4        startColor    {1, 1, 1, 1};
    Vec4        endColor      {1, 1, 1, 0};
    float       scrollSpeed   = 4.0f;
    std::vector<TrailPoint> points;
    float       elapsed = 0.f;
};

class TrailRenderer {
public:
    int  addRibbon(const TrailRibbon& r);
    void pushPoint(int ribbonId, const Vec3& worldPos);
    void update(float dt);

    std::vector<TrailRibbon> ribbons;
};

} // namespace dionite::vfx
