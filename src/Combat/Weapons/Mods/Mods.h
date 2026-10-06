// ============================================================================
// Dionite — Combat: Weapon mod registry (barrels, scopes, mags, stocks, muzzles, grips)
// ============================================================================
#pragma once
#include "Combat/Weapons/WeaponBase.h"
#include <vector>
#include <unordered_map>

namespace dionite::combat {

class ModRegistry {
public:
    static ModRegistry& instance() { static ModRegistry r; r.init(); return r; }

    const std::vector<WeaponMod>& allOfSlot(const std::string& slot) const {
        static const std::vector<WeaponMod> empty;
        auto it = bySlot_.find(slot);
        return it == bySlot_.end() ? empty : it->second;
    }
    const WeaponMod* find(const std::string& id) const {
        auto it = byId_.find(id);
        return it == byId_.end() ? nullptr : &it->second;
    }

private:
    void init() {
        if (!byId_.empty()) return;
        add({"barrel_long",  "Long Barrel",  "barrel",   1.20f, 1.00f, 0.7f, 0, 1.1f, "lensflare"});
        add({"barrel_short", "Stub Barrel",  "barrel",   0.95f, 0.85f, 1.4f, 0, 0.9f, ""});
        add({"scope_arcane", "Arcane Scope", "scope",    1.10f, 1.00f, 0.6f, 0, 1.0f, "scope_glow"});
        add({"mag_extended", "Extended Magazine", "magazine", 1.00f, 1.00f, 1.0f, 8, 1.0f, ""});
        add({"mag_drum",     "Drum Magazine",    "magazine", 1.00f, 1.10f, 1.0f, 24, 1.0f, ""});
        add({"stock_carbon", "Carbon Stock",     "stock",    1.05f, 1.00f, 0.85f, 0, 1.0f, ""});
        add({"muzzle_brake", "Muzzle Brake",     "muzzle",   1.00f, 1.00f, 0.7f, 0, 1.0f, ""});
        add({"muzzle_silencer", "Soul Silencer", "muzzle",   1.00f, 1.05f, 0.95f, 0, 1.0f, "silencer_glow"});
        add({"grip_arcane",  "Arcane Grip",      "grip",     1.00f, 1.08f, 0.90f, 0, 1.0f, ""});
    }

    void add(const WeaponMod& m) { byId_[m.id] = m; bySlot_[m.slot].push_back(m); }

    std::unordered_map<std::string, WeaponMod> byId_;
    std::unordered_map<std::string, std::vector<WeaponMod>> bySlot_;
};

} // namespace dionite::combat
