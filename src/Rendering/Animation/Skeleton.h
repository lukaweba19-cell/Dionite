// Skeleton.h — Bone hierarchy for skeletal animation.
// AAA pipeline: GLTF/FBX-baked skeletons up to 96 bones per character, dual-quaternion
// skinning to avoid candy-wrapper collapse on extreme twists. Compatible with Maya / Blender.
#pragma once
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace dionite::anim {
using math::Vec3;
using math::Vec4;
using math::Mat4;

struct Bone {
    std::string name;
    int         parent = -1;          // index into Skeleton::bones, -1 == root
    Mat4        inverseBind;          // model-space → bone-space
    Vec3        localTranslation{0,0,0};
    Vec4        localRotation{0,0,0,1}; // quaternion (xyzw)
    Vec3        localScale{1,1,1};
};

class Skeleton {
public:
    static constexpr int kMaxBones = 96;

    std::vector<Bone> bones;
    std::unordered_map<std::string, int> byName;

    int  add(const Bone& b);
    int  find(const std::string& n) const;
    Mat4 globalTransform(int boneIndex) const;       // composes local TRS up to root
    void resetPose();                                // restore bind pose
    void computeSkinningMatrices(std::vector<Mat4>& outPalette) const; // size == bones.size()
};

} // namespace dionite::anim
