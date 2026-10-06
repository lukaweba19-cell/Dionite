// ============================================================================
// Dionite — Loot: Rarity color mapping + helpers
// ============================================================================
#pragma once
#include "Loot/Items/ItemBase.h"

namespace dionite::loot {

inline const char* rarityHex(Rarity r) {
    switch (r) {
        case Rarity::Common:    return "#B0B0B0";
        case Rarity::Magic:     return "#3B82F6";
        case Rarity::Rare:      return "#FBBF24";
        case Rarity::Epic:      return "#A855F7";
        case Rarity::Legendary: return "#F97316";
        case Rarity::Mythic:    return "#E11D48";
    }
    return "#FFFFFF";
}

} // namespace dionite::loot
