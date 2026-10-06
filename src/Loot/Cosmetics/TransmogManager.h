// TransmogManager.h — Lets the player override the visual mesh/textures of any equipped
// item with one they've unlocked (looted a piece once → its appearance is unlocked
// forever for any future character on the account). Dyes recolor allowed channels.
#pragma once
#include "CosmeticItem.h"
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace dionite::cosmetics {

struct DyeChannel { float r=1,g=1,b=1; };

struct TransmogLoadout {
    std::unordered_map<CosmeticSlot, std::string> appearance; // slot → cosmeticId
    std::unordered_map<CosmeticSlot, std::array<DyeChannel,4>> dyes; // up to 4 channels
};

class TransmogManager {
public:
    void unlockAppearance(const std::string& cosmeticId);     // call when a player loots an item
    bool isUnlocked(const std::string& cosmeticId) const;

    bool applyTransmog(TransmogLoadout& loadout, CosmeticSlot slot, const std::string& cosmeticId);
    bool applyDye(TransmogLoadout& loadout, CosmeticSlot slot, int channel, const DyeChannel& d);
    void clearTransmog(TransmogLoadout& loadout, CosmeticSlot slot);

    const std::unordered_set<std::string>& unlocked() const { return unlockedIds; }
private:
    std::unordered_set<std::string> unlockedIds;
};

} // namespace dionite::cosmetics
