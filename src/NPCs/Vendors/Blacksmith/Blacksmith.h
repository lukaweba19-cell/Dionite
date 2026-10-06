// ============================================================================
// Dionite — NPC: Blacksmith — repairs, masterworks, upgrades items
// ============================================================================
#pragma once
#include "NPCs/Base/NPCBase.h"
#include "Loot/Items/ItemBase.h"
#include "Loot/Crafting/Crafter.h"

namespace dionite::npc {

class Blacksmith : public NPCBase {
public:
    Blacksmith() : NPCBase("npc_blacksmith", "Brom, the Hammer of Verith", NPCKind::Blacksmith) {
        greetingLines = {
            {"Brom", "Heat, hammer, and patience — that's all the world ever needed."},
            {"Brom", "Bring me something broken. Or something boring. I'll make it scream."},
            {"Brom", "Steel listens. Always listens. People... not so much."},
        };
    }

    // Upgrade weapon damage by % (paid in gold).
    bool repairAndUpgrade(loot::Item& it, int& gold) {
        int cost = (int)(50 + it.damage * 4 * (1 + (int)it.rarity));
        if (gold < cost) return false;
        gold -= cost;
        it.damage *= 1.10f;
        return true;
    }

    bool masterwork(loot::Item& it, int& gold, loot::Crafter& crafter) {
        int cost = 500 * (1 + (int)it.rarity * 2);
        if (gold < cost) return false;
        if (!crafter.masterwork(it)) return false;
        gold -= cost;
        return true;
    }
};

} // namespace dionite::npc
