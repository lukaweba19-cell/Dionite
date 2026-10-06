// ============================================================================
// Dionite — Player: Diablo III/IV-style fixed isometric follow camera.
// Camera is locked at a fixed pitch/yaw above and behind the player; the
// player never rotates the world. Holding Alt + dragging allows a small
// orbital pan (yaw only, clamped) so the player can peek around corners,
// matching Diablo IV's "camera rotation" preference.
// ============================================================================
#pragma once
#include "Core/Math/Vector.h"
#include "Core/Math/Matrix.h"
#include <algorithm>

namespace dionite::player {

struct CameraConfig {
    float pitchDeg     = 55.f;   // looking-down angle (Diablo IV ≈ 50-60°)
    float yawDeg       = 35.f;   // baseline yaw
    float distance     = 18.f;   // distance from player to camera
    float fovYRad      = 1.0472f; // 60° vertical FOV
    float zNear        = 0.1f;
    float zFar         = 400.f;
    float smoothing    = 9.f;
    float maxYawDelta  = 25.f;   // clamp on Alt-drag (±25°)
    bool  cinematicLerp = true;
};

class GameCamera {
public:
    GameCamera() = default;

    // Update follow position. `targetWorldPos` is the player feet position.
    void follow(const math::Vec3& targetWorldPos, float dt) {
        // Apply yaw offset from user pan, clamped
        userYawDeg_ = std::max(-cfg_.maxYawDelta, std::min(cfg_.maxYawDelta, userYawDeg_));

        const float yawRad   = (cfg_.yawDeg + userYawDeg_) * 0.01745329f;
        const float pitchRad =  cfg_.pitchDeg              * 0.01745329f;

        // Spherical to cartesian offset from target
        const float cp = std::cos(pitchRad), sp = std::sin(pitchRad);
        const float cy = std::cos(yawRad),   sy = std::sin(yawRad);
        const math::Vec3 dir{ cp * sy, sp, cp * cy };
        const math::Vec3 desired = targetWorldPos + dir * cfg_.distance;

        if (cfg_.cinematicLerp) {
            const float t = 1.f - std::exp(-cfg_.smoothing * dt);
            position_ = math::Vec3::lerp(position_, desired, t);
            lookAt_   = math::Vec3::lerp(lookAt_,   targetWorldPos, t);
        } else {
            position_ = desired;
            lookAt_   = targetWorldPos;
        }

        if (shakeTime_ > 0.f) {
            shakeTime_ -= dt;
            float k = std::max(0.f, shakeTime_) * shakeIntensity_;
            shakeOffset_ = math::Vec3((std::rand() % 200 - 100) * 0.01f * k,
                                       (std::rand() % 200 - 100) * 0.01f * k,
                                       (std::rand() % 200 - 100) * 0.01f * k);
        } else {
            shakeOffset_ = {};
        }
    }

    void applyUserYaw(float deltaDeg) { userYawDeg_ += deltaDeg; }
    void resetUserYaw() { userYawDeg_ = 0.f; }

    void shake(float seconds, float intensity = 4.f) {
        shakeTime_ = std::max(shakeTime_, seconds);
        shakeIntensity_ = std::max(shakeIntensity_, intensity);
    }

    math::Mat4 viewMatrix() const {
        return math::Mat4::lookAt(position_ + shakeOffset_, lookAt_ + shakeOffset_, {0,1,0});
    }
    math::Mat4 projMatrix(float aspect) const {
        return math::Mat4::perspective(cfg_.fovYRad, aspect, cfg_.zNear, cfg_.zFar);
    }

    // Project a click in NDC (-1..1) to a ray for click-to-move.
    // Returns the world-space ground intersection assuming a flat plane at y=0.
    bool groundFromScreen(float ndcX, float ndcY, float aspect, math::Vec3& outWorld) const {
        // Build the ray analytically from the camera basis; the game runs on a
        // near-flat plane so we intersect y = 0 directly.
        math::Vec3 fwd  = (lookAt_ - position_).normalized();
        math::Vec3 right = fwd.cross({0,1,0}).normalized();
        math::Vec3 up   = right.cross(fwd).normalized();
        float t = std::tan(cfg_.fovYRad * 0.5f);
        math::Vec3 rayDir = (fwd + right * (ndcX * t * aspect) + up * (ndcY * t)).normalized();

        // Intersect with y = 0 plane (Diablo runs on a near-flat plane)
        if (std::abs(rayDir.y) < 1e-4f) return false;
        float dist = -position_.y / rayDir.y;
        if (dist <= 0.f) return false;
        outWorld = position_ + rayDir * dist;
        return true;
    }

    math::Vec3 position() const { return position_ + shakeOffset_; }
    math::Vec3 lookAt()   const { return lookAt_ + shakeOffset_; }
    const CameraConfig& config() const { return cfg_; }
    CameraConfig& config() { return cfg_; }

private:
    CameraConfig cfg_;
    math::Vec3 position_{0, 20, -18};
    math::Vec3 lookAt_{0, 0, 0};
    math::Vec3 shakeOffset_{};
    float userYawDeg_   = 0.f;
    float shakeTime_    = 0.f;
    float shakeIntensity_ = 0.f;
};

} // namespace dionite::player
