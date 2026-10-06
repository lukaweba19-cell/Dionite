// ============================================================================
// Dionite — Skill Loadout & Runtime Cooldown / Resource manager.
//
// Diablo-style 6-button active bar:
//   slot 0: Basic (LMB / RT)         — no cost, no cooldown
//   slot 1: Core  (RMB / LT)         — costs resource
//   slot 2: Defensive (1)            — cooldown
//   slot 3: Mobility (2)             — cooldown
//   slot 4: Utility (3)              — cooldown
//   slot 5: Ultimate (4)             — long cooldown
// Plus up to 4 Passives.
//
// Enforces Diablo rules:
//   - Basic NEVER has a cooldown or cost, generates resource.
//   - Core uses resource and may have small cooldown.
//   - Defensive / Mobility / Utility / Ultimate are gated by cooldown.
// ============================================================================
#pragma once
#include "SkillLibrary.h"
#include "Progression/Classes/ClassRegistry.h"
#include <array>
#include <vector>
#include <string>

namespace dionite::progression {

struct ActiveSlot {
    std::string skillId;
    int rank = 0;
    float cdTimer = 0.f;  // remaining cooldown
};

struct PlayerLoadout {
    ClassId classId = ClassId::Crusader;
    std::array<ActiveSlot, 6> active{};     // Basic / Core / Defensive / Mobility / Utility / Ultimate
    std::array<std::string, 4> passives{};  // up to 4 passive ids
};

struct ResourceState {
    float current = 100.f;
    float maximum = 100.f;
    float regenPerSec = 0.f;
};

class LoadoutManager {
public:
    // Validate that a skill belongs to the right slot for a class.
    static bool fitsSlot(const SkillDefinition& s, int slot) {
        switch (slot) {
            case 0: return s.category == SkillCategory::Basic;
            case 1: return s.category == SkillCategory::Core;
            case 2: return s.category == SkillCategory::Defensive;
            case 3: return s.category == SkillCategory::Mobility;
            case 4: return s.category == SkillCategory::Utility;
            case 5: return s.category == SkillCategory::Ultimate;
            default: return false;
        }
    }

    bool assign(PlayerLoadout& l, int slot, const std::string& skillId) {
        const auto* def = SkillLibrary::instance().find(skillId);
        if (!def || def->classId != l.classId) return false;
        if (!fitsSlot(*def, slot)) return false;
        l.active[slot].skillId = skillId;
        l.active[slot].rank = 0;
        l.active[slot].cdTimer = 0.f;
        return true;
    }

    // Try to cast slot N. Returns true if cast succeeded.
    bool tryCast(PlayerLoadout& l, int slot, ResourceState& res) {
        if (slot < 0 || slot >= 6) return false;
        auto& s = l.active[slot];
        if (s.skillId.empty()) return false;
        const auto* def = SkillLibrary::instance().find(s.skillId);
        if (!def) return false;

        // Diablo rule: basic skills bypass cd & cost
        if (def->category != SkillCategory::Basic) {
            if (s.cdTimer > 0.f) return false;
            const float costMult = def->ranks.empty() ? 1.f : def->ranks[s.rank].resourceCostMult;
            const float cost = def->resourceCost * costMult;
            if (res.current < cost) return false;
            res.current -= cost;
            const float cdMult = def->ranks.empty() ? 1.f : def->ranks[s.rank].cooldownMult;
            s.cdTimer = def->cooldown * cdMult;
        } else {
            // Basics generate resource on hit (we apply it here on cast, simpler)
            res.current = std::min(res.maximum, res.current + def->resourceGenerate);
        }
        return true;
    }

    void tickCooldowns(PlayerLoadout& l, float dt) {
        for (auto& s : l.active) if (s.cdTimer > 0.f) s.cdTimer -= dt;
    }

    void tickResource(ResourceState& res, float dt) {
        if (res.current < res.maximum && res.regenPerSec > 0.f)
            res.current = std::min(res.maximum, res.current + res.regenPerSec * dt);
    }
};

} // namespace dionite::progression
