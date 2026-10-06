// ============================================================================
// Dionite — Combat: Hit-feedback systems (screen shake, hitstop, damage numbers)
// ============================================================================
#pragma once
#include "Core/Math/Vector.h"
#include <vector>
#include <string>
#include <algorithm>

namespace dionite::combat {

struct DamageNumber {
    math::Vec3 position;
    float value;
    float age = 0.f;
    float maxAge = 0.8f;
    bool crit = false;
    std::string colorHex = "#ffffff";
};

class FeedbackSystem {
public:
    void shake(float seconds, float intensity = 4.f) {
        shakeTime = std::max(shakeTime, seconds);
        shakeIntensity = std::max(shakeIntensity, intensity);
    }
    void hitstop(float seconds) { stopTime = std::max(stopTime, seconds); }
    void addDamageNumber(const math::Vec3& p, float v, bool crit, const std::string& color = "#ffffff") {
        numbers.push_back({p, v, 0, 0.8f, crit, color});
    }
    // returns current time-scale (0 if frozen)
    float update(float dt) {
        if (stopTime > 0) { stopTime -= dt; return 0.f; }
        if (shakeTime > 0) shakeTime -= dt;
        for (auto& n : numbers) n.age += dt;
        numbers.erase(std::remove_if(numbers.begin(), numbers.end(),
            [](const DamageNumber& n) { return n.age >= n.maxAge; }), numbers.end());
        return 1.f;
    }
    const std::vector<DamageNumber>& activeNumbers() const { return numbers; }

    float shakeTime = 0;
    float shakeIntensity = 0;
    float stopTime = 0;

private:
    std::vector<DamageNumber> numbers;
};

} // namespace dionite::combat
