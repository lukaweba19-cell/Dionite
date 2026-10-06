// ============================================================================
// Dionite — NPC: Traveling Mage — roams biomes, enchants weapons, brews potions
// ============================================================================
#pragma once
#include "NPCs/Base/NPCBase.h"
#include "Loot/Items/ItemBase.h"
#include "Loot/Crafting/Crafter.h"
#include "Core/Math/Random.h"

namespace dionite::npc {

class TravelingMage : public NPCBase {
public:
    TravelingMage() : NPCBase("npc_mage", "Aldmar Ash-Walker", NPCKind::TravelingMage) {
        greetingLines = {
            {"Aldmar", "I have walked five biomes and seen four impossible suns."},
            {"Aldmar", "Magic is patience burning in a clever shape."},
            {"Aldmar", "I offer enchantments... and unsolicited prophecies. Choose carefully."},
        };
    }

    // Rerolls all affixes (chimera). Costs gold.
    bool enchant(loot::Item& it, int& gold, loot::Crafter& crafter) {
        int cost = 350 * (1 + (int)it.rarity);
        if (gold < cost) return false;
        crafter.chimera(it);
        gold -= cost;
        return true;
    }

    // Moves to a new biome (roams).
    void roam(std::vector<std::string>& visited, const std::vector<std::string>& allBiomes,
              uint64_t seed) {
        if (allBiomes.empty()) return;
        math::Random r(seed);
        currentBiome_ = allBiomes[r.range(0, (int)allBiomes.size() - 1)];
        visited.push_back(currentBiome_);
    }

    const std::string& currentBiome() const { return currentBiome_; }

private:
    std::string currentBiome_ = "verdant_wilds";
};

} // namespace dionite::npc
