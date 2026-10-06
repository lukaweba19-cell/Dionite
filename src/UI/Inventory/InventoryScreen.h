// ============================================================================
// Dionite — UI: Inventory screen (Diablo-style grid + equipment slots)
// ============================================================================
#pragma once
#include "UI/Screens/UIScreen.h"
#include "Loot/Items/ItemBase.h"
#include <vector>
#include <unordered_map>

namespace dionite::ui {

class InventoryScreen : public UIScreen {
public:
    std::vector<loot::Item> bag;
    std::unordered_map<std::string, loot::Item> equipped;
    int gold = 0;

    bool blocksGameplay() const override { return true; }
    std::string id() const override { return "inventory"; }
    void update(float) override {}
    void render() override {}

    bool equip(const std::string& slot, int bagIndex) {
        if (bagIndex < 0 || bagIndex >= (int)bag.size()) return false;
        auto it = equipped.find(slot);
        if (it != equipped.end()) bag.push_back(it->second);
        equipped[slot] = bag[bagIndex];
        bag.erase(bag.begin() + bagIndex);
        return true;
    }
    bool unequip(const std::string& slot) {
        auto it = equipped.find(slot);
        if (it == equipped.end()) return false;
        bag.push_back(it->second);
        equipped.erase(it);
        return true;
    }
};

} // namespace dionite::ui
