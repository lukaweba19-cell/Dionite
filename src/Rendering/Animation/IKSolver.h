// IKSolver.h — Inverse Kinematics for foot planting and look-at gaze.
// Two-bone IK (analytic) for limbs, single-bone aim constraint for head/torso lookat.
// Cheap enough to run on phone GPU at 60Hz for a single character.
#pragma once
#include "Skeleton.h"
#include "Core/Math/Vector.h"

namespace dionite::anim {
using math::Vec3;
using math::Vec4;
using math::Mat4;

class IKSolver {
public:
    // Two-bone IK: rotate shoulder/upper and elbow/lower so the hand/foot reaches `target`.
    // pole = elbow/knee hint in world space (for bend direction).
    static void twoBone(Skeleton& skel,
                        int upperIdx, int lowerIdx, int endEffectorIdx,
                        const Vec3& target, const Vec3& pole);

    // Look-at constraint — rotates the head/torso bone to face `target`.
    static void lookAt(Skeleton& skel, int headIdx, const Vec3& target, float maxYaw, float maxPitch);

    // Foot-plant: raycast against terrain and adjust IK target onto the hit surface.
    // Returns true if a hit was found within `maxLift`.
    static bool footPlant(const Vec3& footWorldPos, float maxLift, Vec3& outTarget);
};

} // namespace dionite::anim
