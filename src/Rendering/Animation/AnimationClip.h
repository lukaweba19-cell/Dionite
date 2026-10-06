// AnimationClip.h — keyframe-based animation track for skeletal meshes.
// AAA pipeline: 30 fps baked clips, cubic-spline tangent interpolation for translation,
// SLERP for rotation, root-motion extraction supported (translation X/Z stripped from root
// when locomotion blends with movement).
#pragma once
#include "Core/Math/Vector.h"
#include <string>
#include <vector>

namespace dionite::anim {
using math::Vec3;
using math::Vec4;

template <typename T>
struct Keyframe { float t = 0.f; T value{}; };

struct BoneTrack {
    int boneIndex = -1;
    std::vector<Keyframe<Vec3>> translation;
    std::vector<Keyframe<Vec4>> rotation;     // quaternions
    std::vector<Keyframe<Vec3>> scale;
};

enum class LoopMode { Once, Loop, PingPong };
enum class RootMotionMode { None, ExtractXZ, ExtractAll };

struct AnimationClip {
    std::string         name;
    float               duration   = 0.f;     // seconds
    float               fps        = 30.f;
    LoopMode            loop       = LoopMode::Loop;
    RootMotionMode      rootMotion = RootMotionMode::None;
    std::vector<BoneTrack> tracks;

    // Discrete event timeline (footstep, attack-hit-frame, vfx-emit, sfx-cue).
    struct Event { float t; std::string tag; };
    std::vector<Event> events;
};

} // namespace dionite::anim
