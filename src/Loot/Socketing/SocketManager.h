// ============================================================================
// Dionite — Loot: Socket manager (insert / remove gems & runes; reset)
// ============================================================================
#pragma once
#include "Loot/Items/ItemBase.h"
#include "Loot/Gems/Gem.h"
#include "Loot/Runes/Rune.h"
#include "Loot/Runes/Words/RuneWords.h"
#include <unordered_map>

namespace dionite::loot {

struct AggregatedStats {
    std::unordered_map<std::string, float> stats;
    std::string activeRuneWord;
};

class SocketManager {
public:
    bool insert(Item& item, int socketIndex, const std::string& gemOrRuneId) {
        if (socketIndex < 0 || socketIndex >= (int)item.sockets.size()) return false;
        if (!item.sockets[socketIndex].empty()) return false; // occupied
        item.sockets[socketIndex] = gemOrRuneId;
        return true;
    }

    bool remove(Item& item, int socketIndex) {
        if (socketIndex < 0 || socketIndex >= (int)item.sockets.size()) return false;
        if (item.sockets[socketIndex].empty()) return false;
        item.sockets[socketIndex].clear();
        return true;
    }

    void clearAll(Item& item) {
        for (auto& s : item.sockets) s.clear();
    }

    AggregatedStats aggregate(const Item& item) const {
        AggregatedStats out;
        // affixes
        for (const auto& a : item.affixes) out.stats[a.stat] += a.amount;
        // gems & runes
        std::vector<std::string> runeOrder;
        for (const auto& sid : item.sockets) {
            if (sid.empty()) continue;
            if (const auto* g = GemLibrary::instance().find(sid)) {
                out.stats[g->stat] += g->amount;
                continue;
            }
            if (const auto* r = RuneLibrary::instance().find(sid)) {
                out.stats[r->stat] += r->amount;
                runeOrder.push_back(sid);
            }
        }
        // rune word
        if (auto* rw = RuneWordLibrary::instance().detect(runeOrder)) {
            for (auto& [stat, amt] : rw->bonus) out.stats[stat] += amt;
            out.activeRuneWord = rw->name;
        }
        return out;
    }
};

} // namespace dionite::loot
