// ============================================================================
// Dionite — Combat: Status effects (burn, slow, shock, poison, bleed, soul-mark)
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace dionite::combat {

enum class StatusKind { Burn, Slow, Shock, Poison, Bleed, SoulMark, Stun };

struct StatusEffect {
    StatusKind kind;
    float duration;
    float intensity; // dmg/sec or slow %
    float tickTimer = 0.f;
};

class StatusManager {
public:
    void apply(const StatusEffect& s) { effects_.push_back(s); }
    // returns total dmg-per-second this frame
    float update(float dt) {
        float dmgThisFrame = 0;
        for (auto& e : effects_) {
            e.duration -= dt;
            if (e.kind == StatusKind::Burn || e.kind == StatusKind::Poison || e.kind == StatusKind::Bleed) {
                e.tickTimer -= dt;
                if (e.tickTimer <= 0) {
                    dmgThisFrame += e.intensity;
                    e.tickTimer = 0.5f;
                }
            }
        }
        effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
            [](const StatusEffect& e) { return e.duration <= 0; }), effects_.end());
        return dmgThisFrame;
    }
    float slowMultiplier() const {
        float m = 1.f;
        for (auto& e : effects_) if (e.kind == StatusKind::Slow) m *= (1.f - e.intensity);
        return std::max(0.1f, m);
    }
    bool stunned() const {
        for (auto& e : effects_) if (e.kind == StatusKind::Stun) return true;
        return false;
    }
    const std::vector<StatusEffect>& list() const { return effects_; }
private:
    std::vector<StatusEffect> effects_;
};

} // namespace dionite::combat
