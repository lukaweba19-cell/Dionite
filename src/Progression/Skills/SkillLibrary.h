// ============================================================================
// Dionite — Progression: Skill definition + classification (Diablo-style)
//
// Categories (and their rules):
//   Basic        — generator. No cost. No cooldown. Always usable.
//   Core         — spender. Costs resource. Usually no/short cooldown.
//   Defensive    — protective. Cooldown-gated. May cost a small resource amount.
//   Mobility     — dash/teleport. Cooldown-gated. May cost resource.
//   Utility      — support/CC. Cooldown-gated. Costs vary.
//   Ultimate     — signature. Long cooldown. Costs full resource pool typically.
//   Passive      — always on; modifies stats/triggers (no cost, no cooldown).
// ============================================================================
#pragma once
#include "Progression/Classes/ClassRegistry.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace dionite::progression {

enum class SkillCategory : uint8_t {
    Basic, Core, Defensive, Mobility, Utility, Ultimate, Passive
};

inline const char* categoryName(SkillCategory c) {
    switch (c) {
        case SkillCategory::Basic:     return "Basic";
        case SkillCategory::Core:      return "Core";
        case SkillCategory::Defensive: return "Defensive";
        case SkillCategory::Mobility:  return "Mobility";
        case SkillCategory::Utility:   return "Utility";
        case SkillCategory::Ultimate:  return "Ultimate";
        case SkillCategory::Passive:   return "Passive";
    }
    return "?";
}

enum class DamageType : uint8_t {
    Physical, Fire, Cold, Shock, Holy, Shadow, Poison, None
};

struct SkillRank {
    int rank;                  // 0..4 typically
    float damageMult;          // multiplier applied to base damage
    float cooldownMult;        // 1.0 default; ranks can reduce
    float resourceCostMult;    // 1.0 default; ranks can reduce
    std::string flavor;        // optional rank flavor text
};

struct SkillDefinition {
    std::string id;
    std::string name;
    ClassId classId;
    SkillCategory category;
    DamageType damageType;
    std::string description;
    float baseDamage;          // 0 for non-damaging utilities
    float resourceCost;        // 0 means no cost
    float resourceGenerate;    // > 0 for basic generators
    float cooldown;            // 0 means no cooldown
    float castTime;            // 0 = instant
    float radius;              // for AoE (0 = single-target)
    float range;               // 0 = melee, > 0 = ranged
    float duration;            // for buffs / DoT
    int   maxRank = 5;
    int   unlockLevel = 1;
    std::vector<SkillRank> ranks;
    std::vector<std::string> tags;   // "melee","ranged","summon","aoe","cc","heal",...
    std::vector<std::string> synergies; // ids of skills that buff this one
    std::string iconAsset;
    std::string animClip;
    std::string sfxId;
    std::string vfxId;
};

class SkillLibrary {
public:
    static SkillLibrary& instance() { static SkillLibrary l; l.init(); return l; }

    const SkillDefinition* find(const std::string& id) const {
        auto it = byId_.find(id);
        return it == byId_.end() ? nullptr : &it->second;
    }

    std::vector<const SkillDefinition*> forClass(ClassId c) const {
        std::vector<const SkillDefinition*> out;
        for (auto& [_, s] : byId_) if (s.classId == c) out.push_back(&s);
        return out;
    }

    std::vector<const SkillDefinition*> forClassAndCategory(ClassId c, SkillCategory cat) const {
        std::vector<const SkillDefinition*> out;
        for (auto& [_, s] : byId_) if (s.classId == c && s.category == cat) out.push_back(&s);
        return out;
    }

    size_t totalCount() const { return byId_.size(); }

private:
    void addRanks(SkillDefinition& s, int max = 5) {
        s.maxRank = max;
        for (int r = 0; r < max; ++r) {
            s.ranks.push_back({r,
                1.f + r * 0.25f,                      // +25% dmg per rank
                std::max(0.5f, 1.f - r * 0.10f),      // -10% cd per rank, floor 0.5
                std::max(0.5f, 1.f - r * 0.08f),      // -8% cost per rank
                r == 0 ? "" : (r == max - 1 ? "Mastery" : "Empowered")
            });
        }
    }

    void add(SkillDefinition s) {
        if (s.ranks.empty()) addRanks(s);
        byId_[s.id] = std::move(s);
    }

    void init() {
        if (!byId_.empty()) return;
        loadCrusader();
        loadNecromancer();
        loadSorcerer();
        loadRanger();
        loadMonk();
    }

    void loadCrusader();
    void loadNecromancer();
    void loadSorcerer();
    void loadRanger();
    void loadMonk();

    std::unordered_map<std::string, SkillDefinition> byId_;
};

} // namespace dionite::progression
