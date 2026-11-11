// ============================================================================
// Dionite — Math: 4x4 column-major matrix + transform helpers (Metal layout)
// ============================================================================
#pragma once
#include "Vector.h"
#include <cmath>
#include <cstring>

namespace dionite::math {

struct Mat4 {
    float m[16]; // column-major

    Mat4() { identity(); }

    void identity() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.f;
    }

    static Mat4 translation(const Vec3& t) {
        Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r;
    }

    static Mat4 scale(const Vec3& s) {
        Mat4 r; r.m[0]=s.x; r.m[5]=s.y; r.m[10]=s.z; return r;
    }

    static Mat4 rotationY(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0]=c;  r.m[2]=s;
        r.m[8]=-s; r.m[10]=c;
        return r;
    }

    static Mat4 rotationX(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[5]=c;  r.m[6]=-s;
        r.m[9]=s;  r.m[10]=c;
        return r;
    }

    static Mat4 perspective(float fovYRad, float aspect, float zNear, float zFar) {
        Mat4 r;
        std::memset(r.m, 0, sizeof(r.m));
        float f = 1.f / std::tan(fovYRad * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = zFar / (zNear - zFar);
        r.m[11] = -1.f;
        r.m[14] = (zNear * zFar) / (zNear - zFar);
        return r;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        Mat4 r;
        r.m[0]=s.x; r.m[4]=s.y; r.m[8]=s.z;
        r.m[1]=u.x; r.m[5]=u.y; r.m[9]=u.z;
        r.m[2]=-f.x; r.m[6]=-f.y; r.m[10]=-f.z;
        r.m[12]=-s.dot(eye);
        r.m[13]=-u.dot(eye);
        r.m[14]=f.dot(eye);
        r.m[15]=1.f;
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int r2 = 0; r2 < 4; ++r2)
                r.m[c * 4 + r2] = m[0*4+r2]*o.m[c*4+0] + m[1*4+r2]*o.m[c*4+1] + m[2*4+r2]*o.m[c*4+2] + m[3*4+r2]*o.m[c*4+3];
        return r;
    }
};

} // namespace dionite::math
