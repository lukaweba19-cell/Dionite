// ============================================================================
// Dionite — Progression: Class system (Diablo III/IV-style)
// Each class has a primary resource, base stats, and a skill kit.
// ============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dionite::progression {

enum class ClassId : uint8_t {
    Crusader,    // Wrath — holy warrior; AshenWastes theme
    Necromancer, // Essence — bone/blood/shadow; SunkenCrypts theme
    Sorcerer,    // Mana — fire/cold/shock; SkyCitadel theme
    Ranger,      // Discipline — bows + traps; VerdantWilds theme
    Monk,        // Spirit — martial arts + holy lightning; FrozenSpire theme
};

enum class ResourceKind : uint8_t { Wrath, Essence, Mana, Discipline, Spirit };

struct ClassDefinition {
    ClassId id;
    std::string codename;
    std::string displayName;
    std::string tagline;
    std::string description;
    ResourceKind resource;
    std::string resourceName;
    std::string resourceColorHex;

    // Base stats at level 1
    float baseHealth;
    float healthPerLevel;
    float baseResource;
    float resourcePerLevel;
    float resourceRegen;     // per second when not casting
    float armor;
    float movementSpeed;     // meters / second

    // Class-affinity damage bonuses (multiplicative)
    float physicalBonus;
    float fireBonus;
    float coldBonus;
    float shockBonus;
    float holyBonus;
    float shadowBonus;
    float poisonBonus;

    std::string startingSkillId;          // basic skill unlocked at level 1
    std::vector<std::string> startingPassiveIds;
    std::string portraitAsset;            // /assets/ui/portraits/<class>.png
    std::string ambientThemeId;           // music id played in character select
    std::string lobbyEmote;                // animation played on idle
};

class ClassRegistry {
public:
    static ClassRegistry& instance() { static ClassRegistry r; r.init(); return r; }
    const std::vector<ClassDefinition>& all() const { return defs_; }
    const ClassDefinition* find(ClassId id) const {
        for (auto& d : defs_) if (d.id == id) return &d;
        return nullptr;
    }
    const ClassDefinition* findByCodename(const std::string& s) const {
        for (auto& d : defs_) if (d.codename == s) return &d;
        return nullptr;
    }
private:
    void init() {
        if (!defs_.empty()) return;
        defs_.push_back({
            ClassId::Crusader, "crusader", "Crusader",
            "Bulwark of the Forgotten Light",
            "Plate-clad zealot wielding hammer and word. Generates Wrath in battle and unleashes holy judgement.",
            ResourceKind::Wrath, "Wrath", "#F97316",
            /*hp*/ 130.f, 8.f,  /*res*/ 100.f, 0.f, 0.f,
            /*armor*/ 22.f, /*ms*/ 4.8f,
            /*phys*/ 1.10f, 1.00f, 1.00f, 1.00f, 1.25f, 0.90f, 1.00f,
            "cru_smite", {"cru_pas_zeal", "cru_pas_bulwark"},
            "ui/portraits/crusader.png", "music_crusader_lobby", "anim_idle_crusader"
        });
        defs_.push_back({
            ClassId::Necromancer, "necromancer", "Necromancer",
            "Shepherd of the Pale Court",
            "Master of bone, blood, and shadow. Spends Essence to raise minions and detonate corpses.",
            ResourceKind::Essence, "Essence", "#A855F7",
            110.f, 6.f,  120.f, 1.f, 5.f,
            14.f, 4.6f,
            1.00f, 0.95f, 1.00f, 1.05f, 0.80f, 1.30f, 1.20f,
            "nec_bone_splinters", {"nec_pas_decrepify", "nec_pas_undeath"},
            "ui/portraits/necromancer.png", "music_necro_lobby", "anim_idle_necro"
        });
        defs_.push_back({
            ClassId::Sorcerer, "sorcerer", "Sorcerer",
            "Storm-touched Heretic",
            "Elemental savant — fire, cold, and lightning bend to her will. Lowest health, highest spell damage.",
            ResourceKind::Mana, "Mana", "#3B82F6",
            90.f, 5.f,  150.f, 2.f, 10.f,
            8.f, 5.0f,
            0.85f, 1.30f, 1.30f, 1.30f, 1.00f, 1.00f, 1.00f,
            "sor_frost_bolt", {"sor_pas_elemental_attune", "sor_pas_glass_cannon"},
            "ui/portraits/sorcerer.png", "music_sorc_lobby", "anim_idle_sorc"
        });
        defs_.push_back({
            ClassId::Ranger, "ranger", "Ranger",
            "Whisper of the Verdant",
            "Bow + trap specialist. Discipline fuels evasive maneuvers and precision shots.",
            ResourceKind::Discipline, "Discipline", "#16A34A",
            105.f, 6.f,  100.f, 0.f, 0.f,
            12.f, 5.2f,
            1.20f, 1.10f, 1.10f, 1.10f, 0.95f, 1.00f, 1.20f,
            "ran_quick_shot", {"ran_pas_keen_eye", "ran_pas_softfoot"},
            "ui/portraits/ranger.png", "music_ranger_lobby", "anim_idle_ranger"
        });
        defs_.push_back({
            ClassId::Monk, "monk", "Monk",
            "Wanderer of the Frozen Spire",
            "Fists, feet, and prayer. Spirit accumulates and bursts in storm-touched combos.",
            ResourceKind::Spirit, "Spirit", "#38BDF8",
            115.f, 7.f,  100.f, 0.f, 2.f,
            16.f, 5.4f,
            1.15f, 1.05f, 1.10f, 1.20f, 1.20f, 0.95f, 1.00f,
            "mon_crippling_wave", {"mon_pas_chant_of_resonance", "mon_pas_seize_the_initiative"},
            "ui/portraits/monk.png", "music_monk_lobby", "anim_idle_monk"
        });
    }
    std::vector<ClassDefinition> defs_;
};

} // namespace dionite::progression
