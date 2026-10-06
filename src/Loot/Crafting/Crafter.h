// ============================================================================
// Dionite — Loot: Crafting (Chimera reroll, Masterwork upgrade, Transmute)
// ============================================================================
#pragma once
#include "Loot/Items/ItemBase.h"
#include "Loot/RaritySystem/Rarity.h"
#include "Core/Math/Random.h"

namespace dionite::loot {

struct CraftRecipe {
    std::string id;
    std::string name;
    int goldCost;
    int materialCost;
    std::string description;
};

class Crafter {
public:
    Crafter(uint64_t seed) : rng_(seed) {}

    // Chimera: rerolls all affixes on the item (preserves rarity & sockets).
    void chimera(Item& it) {
        int n = (int)it.affixes.size();
        it.affixes.clear();
        LootRoller lr(rng_.next());
        Item tmp = lr.rollWeapon(it.ilvl, 0.f);
        for (int i = 0; i < n && i < (int)tmp.affixes.size(); ++i)
            it.affixes.push_back(tmp.affixes[i]);
    }

    // Masterwork: upgrades item rarity by one tier (cannot exceed Mythic).
    bool masterwork(Item& it) {
        if (it.rarity == Rarity::Mythic) return false;
        it.rarity = (Rarity)((int)it.rarity + 1);
        // Grow sockets and add an affix
        int newSockets = socketsFor(it.rarity);
        while ((int)it.sockets.size() < newSockets) it.sockets.push_back("");
        it.socketCount = newSockets;
        it.damage *= 1.18f;
        return true;
    }

    // Transmute: convert 3 commons to a magic; recursive for tiers.
    bool transmute(std::vector<Item>& materials, Item& out) {
        if (materials.size() < 3) return false;
        Rarity baseR = materials[0].rarity;
        for (auto& m : materials) if (m.rarity != baseR) return false;
        if (baseR == Rarity::Mythic) return false;
        LootRoller lr(rng_.next());
        out = lr.rollWeapon(materials[0].ilvl, 0.f);
        out.rarity = (Rarity)((int)baseR + 1);
        out.socketCount = socketsFor(out.rarity);
        out.sockets.assign(out.socketCount, "");
        return true;
    }

private:
    math::Random rng_;
};

} // namespace dionite::loot
