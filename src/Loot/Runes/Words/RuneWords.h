// ============================================================================
// Dionite — Loot: Rune Words (exact ordered rune combinations grant huge bonuses)
// ============================================================================
#pragma once
#include "Loot/Runes/Rune.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace dionite::loot {

struct RuneWord {
    std::string id;
    std::string name;
    std::vector<std::string> runes; // ordered ids
    std::unordered_map<std::string, float> bonus;
    std::string description;
};

class RuneWordLibrary {
public:
    static RuneWordLibrary& instance() { static RuneWordLibrary r; r.init(); return r; }
    const std::vector<RuneWord>& all() const { return words_; }

    // Detects rune word from ordered socketed rune-ids. nullptr if none.
    const RuneWord* detect(const std::vector<std::string>& socketIds) const {
        if (socketIds.empty()) return nullptr;
        for (auto& w : words_) {
            if (w.runes.size() != socketIds.size()) continue;
            bool match = true;
            for (size_t i = 0; i < w.runes.size(); ++i)
                if (w.runes[i] != socketIds[i]) { match = false; break; }
            if (match) return &w;
        }
        return nullptr;
    }

private:
    void init() {
        if (!words_.empty()) return;
        words_ = {
            {"rw_doom",   "DOOM",   {"rune_sol","rune_amn","rune_ral"},
             {{"dmgPct",0.70f},{"lifesteal",0.10f},{"firePct",0.30f}},
             "Sol + Amn + Ral — a terrible, swift sword."},
            {"rw_spirit", "SPIRIT", {"rune_tir","rune_thul","rune_ort"},
             {{"manaMax",50.f},{"coldPct",0.40f},{"shockPct",0.40f}},
             "Tir + Thul + Ort — channel the storm."},
            {"rw_grief",  "GRIEF",  {"rune_eth","rune_amn","rune_shael"},
             {{"dmgPct",1.0f},{"fireRatePct",0.4f},{"lifesteal",0.08f}},
             "Eth + Amn + Shael — devastating attacks."},
            {"rw_oath",   "OATH",   {"rune_el","rune_sol","rune_nef"},
             {{"critChance",0.15f},{"dmgPct",0.40f},{"knockback",2.f}},
             "El + Sol + Nef — sworn devastation."},
            {"rw_enigma", "ENIGMA", {"rune_thul","rune_tir","rune_el","rune_sol"},
             {{"dmgPct",0.5f},{"manaMax",75.f},{"critChance",0.1f}},
             "Thul + Tir + El + Sol — the unraveler."},
        };
    }
    std::vector<RuneWord> words_;
};

} // namespace dionite::loot
