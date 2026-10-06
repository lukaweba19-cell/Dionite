// ============================================================================
// Dionite — NPC: General Merchant — buys/sells consumables and basic gear
// ============================================================================
#pragma once
#include "NPCs/Base/NPCBase.h"
#include "Loot/Items/ItemBase.h"
#include <vector>

namespace dionite::npc {

struct ShopOffer {
    loot::Item item;
    int price;
};

class GeneralMerchant : public NPCBase {
public:
    GeneralMerchant() : NPCBase("npc_merchant", "Old Pell", NPCKind::GeneralMerchant) {
        greetingLines = {
            {"Old Pell", "Coin for goods, goods for coin. Don't make it complicated."},
            {"Old Pell", "Cheap potions, fine arrows. Don't touch what you can't afford."},
        };
    }

    void restock(int playerLevel, loot::LootRoller& roller, int slots = 6) {
        offers_.clear();
        for (int i = 0; i < slots; ++i) {
            auto it = roller.rollWeapon(playerLevel, 0.f);
            int price = (int)(80 + it.damage * 4 * (1 + (int)it.rarity));
            offers_.push_back({it, price});
        }
    }

    bool buy(int offerIdx, int& gold, loot::Item& out) {
        if (offerIdx < 0 || offerIdx >= (int)offers_.size()) return false;
        if (gold < offers_[offerIdx].price) return false;
        gold -= offers_[offerIdx].price;
        out = offers_[offerIdx].item;
        offers_.erase(offers_.begin() + offerIdx);
        return true;
    }

    int sell(loot::Item& it, int& gold) {
        int value = (int)(20 + it.damage * 2 * (1 + (int)it.rarity));
        gold += value;
        return value;
    }

    const std::vector<ShopOffer>& offers() const { return offers_; }

private:
    std::vector<ShopOffer> offers_;
};

} // namespace dionite::npc
