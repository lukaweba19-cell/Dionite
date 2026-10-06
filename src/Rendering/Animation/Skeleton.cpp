#include "Skeleton.h"
#include <cassert>

namespace dionite::anim {

static Mat4 trsToMatrix(const Vec3& t, const Vec4& q, const Vec3& s) {
    // quaternion → rotation matrix (column-major Mat4)
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;

    Mat4 m;
    m.m[0]  = (1.f - 2.f*(yy+zz)) * s.x;
    m.m[1]  = 2.f*(xy+wz)         * s.x;
    m.m[2]  = 2.f*(xz-wy)         * s.x;
    m.m[3]  = 0.f;
    m.m[4]  = 2.f*(xy-wz)         * s.y;
    m.m[5]  = (1.f - 2.f*(xx+zz)) * s.y;
    m.m[6]  = 2.f*(yz+wx)         * s.y;
    m.m[7]  = 0.f;
    m.m[8]  = 2.f*(xz+wy)         * s.z;
    m.m[9]  = 2.f*(yz-wx)         * s.z;
    m.m[10] = (1.f - 2.f*(xx+yy)) * s.z;
    m.m[11] = 0.f;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z; m.m[15] = 1.f;
    return m;
}

int Skeleton::add(const Bone& b) {
    int i = (int)bones.size();
    bones.push_back(b);
    byName[b.name] = i;
    return i;
}

int Skeleton::find(const std::string& n) const {
    auto it = byName.find(n);
    return it == byName.end() ? -1 : it->second;
}

Mat4 Skeleton::globalTransform(int idx) const {
    assert(idx >= 0 && idx < (int)bones.size());
    Mat4 local = trsToMatrix(bones[idx].localTranslation,
                             bones[idx].localRotation,
                             bones[idx].localScale);
    if (bones[idx].parent < 0) return local;
    return globalTransform(bones[idx].parent) * local;
}

void Skeleton::resetPose() {
    for (auto& b : bones) {
        b.localTranslation = {0,0,0};
        b.localRotation    = {0,0,0,1};
        b.localScale       = {1,1,1};
    }
}

void Skeleton::computeSkinningMatrices(std::vector<Mat4>& palette) const {
    palette.resize(bones.size());
    // Two-pass: compute global, then global * inverseBind
    std::vector<Mat4> globals(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) globals[i] = globalTransform((int)i);
    for (size_t i = 0; i < bones.size(); ++i)
        palette[i] = globals[i] * bones[i].inverseBind;
}

} // namespace dionite::anim
