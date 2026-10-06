// ============================================================================
// Dionite — Loot: Chests (regular, mimics, vaults) with weighted drops
// ============================================================================
#pragma once
#include "Loot/Items/ItemBase.h"
#include "Core/Math/Random.h"
#include <vector>
#include <string>

namespace dionite::loot {

enum class ChestKind { Wooden, Iron, Gilded, Vault, Mimic };

struct Chest {
    ChestKind kind;
    int seed;
    bool opened = false;
    int playerLevel = 1;
    bool isMimic() const { return kind == ChestKind::Mimic; }
};

struct ChestDrop {
    std::vector<Item> items;
    int gold = 0;
    int runes = 0;
    int gems = 0;
};

class ChestSystem {
public:
    ChestSystem(uint64_t seed) : rng_(seed) {}

    // Returns false if it's a mimic and triggers combat instead.
    bool open(Chest& c, ChestDrop& out, LootRoller& roller) {
        if (c.opened) return false;
        c.opened = true;
        if (c.kind == ChestKind::Mimic) return false; // caller spawns mimic mob
        int items = 0; float luck = 0.f;
        switch (c.kind) {
            case ChestKind::Wooden:  items = 1; out.gold = rng_.range(20, 80);    luck = 0.0f; break;
            case ChestKind::Iron:    items = 2; out.gold = rng_.range(80, 200);   luck = 0.5f; break;
            case ChestKind::Gilded:  items = 3; out.gold = rng_.range(200, 500);  luck = 1.5f; break;
            case ChestKind::Vault:   items = 5; out.gold = rng_.range(500, 1500); luck = 3.0f; break;
            default: break;
        }
        for (int i = 0; i < items; ++i) out.items.push_back(roller.rollWeapon(c.playerLevel, luck));
        out.runes = rng_.range(0, items);
        out.gems = rng_.range(0, items + 1);
        return true;
    }

private:
    math::Random rng_;
};

} // namespace dionite::loot
