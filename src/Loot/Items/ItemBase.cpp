#include "ItemBase.h"
#include <algorithm>
#include <ctime>

namespace dionite::loot {

static const char* baseIds[] = {"pistol","smg","shotgun","rifle","launcher","staff","frostwand"};
static const std::vector<Affix> affixPool = {
    {"cruel","Cruel","dmgPct",0.30f},
    {"savage","Savage","dmgPct",0.18f},
    {"razor","Razor","critChance",0.08f},
    {"merciless","Merciless","critMult",0.5f},
    {"fervent","Fervent","firePct",0.18f},
    {"vampiric","Vampiric","lifesteal",0.06f},
    {"piercing","Piercing","pierce",1.f},
    {"echoing","Echoing","echo",0.25f},
    {"glacial","Glacial","slow",0.35f},
};

Item LootRoller::rollWeapon(int level, float luck) {
    Item it;
    it.baseId = baseIds[rng_.range(0, 6)];
    it.kind = "weapon";
    it.rarity = rollRarity(luck);
    it.ilvl = level;
    it.damage = (10.f + level * 1.2f) * (1.f + (int)it.rarity * 0.18f);
    it.socketCount = socketsFor(it.rarity);
    it.sockets.assign(it.socketCount, "");
    int affixCount = affixesFor(it.rarity);
    std::vector<int> used;
    for (int i = 0; i < affixCount; ++i) {
        int idx;
        int tries = 0;
        do { idx = rng_.range(0, (int)affixPool.size() - 1); tries++; }
        while (std::find(used.begin(), used.end(), idx) != used.end() && tries < 10);
        used.push_back(idx);
        Affix a = affixPool[idx];
        a.amount *= (1.f + level * 0.04f);
        it.affixes.push_back(a);
    }
    it.name = std::string(rarityName(it.rarity)) + " " + it.baseId;
    it.uid = "it_" + std::to_string((uint64_t)std::time(nullptr)) + "_" + std::to_string(rng_.range(0, 99999));
    return it;
}

} // namespace dionite::loot
