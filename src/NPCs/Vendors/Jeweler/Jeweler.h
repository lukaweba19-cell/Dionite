// ============================================================================
// Dionite — NPC: Jeweler — sockets gems/runes, removes them, identifies items
// ============================================================================
#pragma once
#include "NPCs/Base/NPCBase.h"
#include "Loot/Items/ItemBase.h"
#include "Loot/Socketing/SocketManager.h"

namespace dionite::npc {

class Jeweler : public NPCBase {
public:
    Jeweler() : NPCBase("npc_jeweler", "Veska Wirefinger", NPCKind::Jeweler) {
        greetingLines = {
            {"Veska", "Sparkles, sparkles. Show me the goods."},
            {"Veska", "Sockets are stories. Tell me yours."},
            {"Veska", "Take it out? Cost you. Always costs you, dear."},
        };
    }

    bool socket(loot::Item& it, int idx, const std::string& gemOrRuneId, int& gold) {
        int cost = 100;
        if (gold < cost) return false;
        loot::SocketManager sm;
        if (!sm.insert(it, idx, gemOrRuneId)) return false;
        gold -= cost;
        return true;
    }

    bool unsocket(loot::Item& it, int idx, int& gold, std::string& removedId) {
        int cost = 250;
        if (gold < cost) return false;
        loot::SocketManager sm;
        if (idx < 0 || idx >= (int)it.sockets.size()) return false;
        removedId = it.sockets[idx];
        if (!sm.remove(it, idx)) return false;
        gold -= cost;
        return true;
    }
};

} // namespace dionite::npc
