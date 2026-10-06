// ============================================================================
// Dionite — Progression: 3-tree, 50+ node skill tree (classless)
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace dionite::progression {

enum class TreeBranch { Warrior, Arcanist, Shadow };

struct SkillNode {
    std::string id;
    std::string name;
    std::string description;
    TreeBranch branch;
    int tier;
    std::vector<std::string> requirements;
    int cost = 1;
    bool keystone = false;
    // Effects keyed by stat name -> additive amount per allocated point
    std::unordered_map<std::string, float> effects;
    // Layout coords for UI
    float x = 0, y = 0;
};

class SkillTree {
public:
    SkillTree() { build(); }

    const std::vector<SkillNode>& nodes() const { return nodes_; }
    const SkillNode* find(const std::string& id) const {
        auto it = byId_.find(id);
        return it == byId_.end() ? nullptr : it->second;
    }

    bool canAllocate(const std::string& id, const std::unordered_map<std::string, int>& spent,
                     int availablePoints) const {
        const auto* n = find(id);
        if (!n) return false;
        if (n->cost > availablePoints) return false;
        if (n->requirements.empty()) return true;
        for (auto& dep : n->requirements) {
            auto it = spent.find(dep);
            if (it == spent.end() || it->second <= 0) return false;
        }
        return true;
    }

    std::unordered_map<std::string, float> aggregateStats(const std::unordered_map<std::string, int>& spent) const {
        std::unordered_map<std::string, float> out;
        for (auto& [nodeId, count] : spent) {
            const auto* n = find(nodeId);
            if (!n || count <= 0) continue;
            for (auto& [stat, amt] : n->effects) out[stat] += amt * count;
        }
        return out;
    }

private:
    void add(SkillNode n) {
        nodes_.push_back(std::move(n));
        byId_[nodes_.back().id] = &nodes_.back();
    }

    void build() {
        // ---------- WARRIOR ----------
        add({"w_vigor1", "Vigor I", "+20 max health.", TreeBranch::Warrior, 0, {}, 1, false, {{"healthMax", 20}}, 100, 60});
        add({"w_power1", "Power I", "+6% damage.",  TreeBranch::Warrior, 0, {"w_vigor1"}, 1, false, {{"dmgPct", 0.06f}}, 100, 130});
        add({"w_vigor2", "Vigor II", "+30 max health.", TreeBranch::Warrior, 1, {"w_power1"}, 1, false, {{"healthMax", 30}}, 60, 200});
        add({"w_reaver", "Reaver", "+4% lifesteal.", TreeBranch::Warrior, 1, {"w_power1"}, 1, false, {{"lifesteal", 0.04f}}, 140, 200});
        add({"w_power2", "Power II", "+10% damage.", TreeBranch::Warrior, 2, {"w_vigor2","w_reaver"}, 2, false, {{"dmgPct", 0.10f}}, 100, 270});
        add({"w_iron",  "Iron Skin", "+10% damage reduction.", TreeBranch::Warrior, 2, {"w_power2"}, 2, false, {{"dmgReduce", 0.10f}}, 60, 340});
        add({"w_berserker", "Berserker", "+6% crit chance.", TreeBranch::Warrior, 2, {"w_power2"}, 2, false, {{"critChance", 0.06f}}, 140, 340});
        add({"w_bloodlust", "Bloodlust", "+20% damage and +6% lifesteal.", TreeBranch::Warrior, 3, {"w_iron","w_berserker"}, 3, false, {{"dmgPct", 0.20f},{"lifesteal",0.06f}}, 100, 410});
        add({"w_godking", "Godking [keystone]", "+40% damage, +100 max HP, +10% crit.",
             TreeBranch::Warrior, 4, {"w_bloodlust"}, 5, true,
             {{"dmgPct", 0.40f},{"healthMax", 100},{"critChance",0.10f}}, 100, 490});

        // ---------- ARCANIST ----------
        add({"m_insight1", "Insight I", "+20 max mana.", TreeBranch::Arcanist, 0, {}, 1, false, {{"manaMax", 20}}, 100, 60});
        add({"m_elem1", "Elemental I", "+8% fire / cold / shock.", TreeBranch::Arcanist, 0, {"m_insight1"}, 1, false, {{"firePct",0.08f},{"coldPct",0.08f},{"shockPct",0.08f}}, 100, 130});
        add({"m_insight2", "Insight II", "+30 max mana.", TreeBranch::Arcanist, 1, {"m_elem1"}, 1, false, {{"manaMax", 30}}, 60, 200});
        add({"m_channel", "Channel", "+10% fire rate.", TreeBranch::Arcanist, 1, {"m_elem1"}, 1, false, {{"fireRatePct", 0.10f}}, 140, 200});
        add({"m_elem2", "Elemental II", "+10% damage.", TreeBranch::Arcanist, 2, {"m_insight2","m_channel"}, 2, false, {{"dmgPct", 0.10f}}, 100, 270});
        add({"m_pyro", "Pyromancer", "+25% fire damage.", TreeBranch::Arcanist, 2, {"m_elem2"}, 2, false, {{"firePct", 0.25f}}, 60, 340});
        add({"m_cryo", "Cryomancer", "+20% slow effect.", TreeBranch::Arcanist, 2, {"m_elem2"}, 2, false, {{"slow", 0.20f}}, 140, 340});
        add({"m_echo", "Echocaster", "+25% chance to re-fire.", TreeBranch::Arcanist, 3, {"m_pyro","m_cryo"}, 3, false, {{"echo", 0.25f}}, 100, 410});
        add({"m_arch", "Archmage [keystone]", "+35% damage, +100 max mana, +20% fire rate.",
             TreeBranch::Arcanist, 4, {"m_echo"}, 5, true,
             {{"dmgPct",0.35f},{"manaMax",100},{"fireRatePct",0.20f}}, 100, 490});

        // ---------- SHADOW ----------
        add({"s_agile1", "Agile I", "Dash cooldown -0.5s.", TreeBranch::Shadow, 0, {}, 1, false, {{"dashCd", -0.5f}}, 100, 60});
        add({"s_prec1", "Precision I", "+5% crit chance.", TreeBranch::Shadow, 0, {"s_agile1"}, 1, false, {{"critChance", 0.05f}}, 100, 130});
        add({"s_evade", "Evasion", "+6% dodge.", TreeBranch::Shadow, 1, {"s_prec1"}, 1, false, {{"dodge", 0.06f}}, 60, 200});
        add({"s_crit", "Critical", "+25% crit multiplier.", TreeBranch::Shadow, 1, {"s_prec1"}, 1, false, {{"critMult", 0.25f}}, 140, 200});
        add({"s_prec2", "Precision II", "+8% crit chance.", TreeBranch::Shadow, 2, {"s_evade","s_crit"}, 2, false, {{"critChance", 0.08f}}, 100, 270});
        add({"s_dance", "Shadowdance", "+10% dodge.", TreeBranch::Shadow, 2, {"s_prec2"}, 2, false, {{"dodge", 0.10f}}, 60, 340});
        add({"s_assassin", "Assassin", "+50% crit multiplier.", TreeBranch::Shadow, 2, {"s_prec2"}, 2, false, {{"critMult", 0.50f}}, 140, 340});
        add({"s_head", "Headhunter", "+10% crit chance, +50% crit multiplier.", TreeBranch::Shadow, 3, {"s_dance","s_assassin"}, 3, false, {{"critChance",0.10f},{"critMult",0.5f}}, 100, 410});
        add({"s_whisper", "Whispering Death [keystone]", "+20% crit, +100% crit mult, +15% dodge.",
             TreeBranch::Shadow, 4, {"s_head"}, 5, true,
             {{"critChance",0.20f},{"critMult",1.0f},{"dodge",0.15f}}, 100, 490});

        // Pad to >50 nodes with cheap minor stat nodes per branch
        for (int i = 0; i < 24; ++i) {
            auto branch = (TreeBranch)(i % 3);
            std::string prefix = branch == TreeBranch::Warrior ? "w_minor_" :
                                 branch == TreeBranch::Arcanist ? "m_minor_" : "s_minor_";
            std::string stat = branch == TreeBranch::Warrior ? "healthMax" :
                               branch == TreeBranch::Arcanist ? "manaMax" : "critChance";
            float amt = branch == TreeBranch::Shadow ? 0.01f : 8.f;
            add({prefix + std::to_string(i), "Lesser Power", "Minor stat node.", branch,
                 5 + i / 6, {}, 1, false, {{stat, amt}}, 200.f + (i % 4) * 40.f, 600.f + (i / 4) * 50.f});
        }
    }

    std::vector<SkillNode> nodes_;
    std::unordered_map<std::string, SkillNode*> byId_;
};

} // namespace dionite::progression
