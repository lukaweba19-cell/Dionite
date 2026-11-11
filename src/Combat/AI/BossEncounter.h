// ============================================================================
// Dionite — Boss Encounter Director (Diablo IV-style cinematic boss fights).
// Handles arena setup, intro cutscene, telegraphed mechanics, phase transition
// VFX, achievement triggers, post-kill cache reveal.
// ============================================================================
#pragma once
#include "Core/Math/Vector.h"
#include "Combat/AI/BossAI.h"
#include <string>
#include <vector>
#include <functional>

namespace dionite::combat {

struct BossArenaProp {
    math::Vec3 position;
    std::string assetId;    // "arena_pillar", "soul_brazier", "boss_throne", "rune_circle"
    float yaw = 0.f;
    float scale = 1.f;
    bool isLightSource = false;
    math::Vec3 lightColor{1.f, 0.4f, 0.2f};
    float lightRadius = 12.f;
};

struct BossCinematic {
    std::string introCameraPath;     // animation asset
    std::string introMusicId;
    std::string introVoiceId;
    std::vector<std::string> introTextLines;
    float       letterboxSeconds = 4.f;
};

struct BossMechanic {
    std::string id;
    std::string name;
    std::string description;
    float       telegraphSeconds = 1.2f;
    float       cooldown = 8.f;
    std::string telegraphVfx;       // "ring_red", "cone_orange", "rectangle_blue"
    std::string impactVfx;
    std::string sfxWarning;
    std::string sfxImpact;
    float       damage;
    float       radius;
};

struct BossEncounter {
    std::string id;
    std::string displayName;
    std::string subtitle;            // "Of the Shattered Ashes"
    std::string biomeId;
    math::Vec3  arenaCenter;
    float       arenaRadius = 36.f;
    std::vector<BossArenaProp> props;
    std::vector<BossMechanic>  phase1;
    std::vector<BossMechanic>  phase2;
    BossCinematic cinematic;
    std::string bossPortrait;
    std::string lootCache;          // "cache_ash_tyrant"
    std::string nameplateColor = "#E11D48";
    bool        showNameplate = true;
    bool        zoneWideAnnouncement = false;
    std::function<void()> onDefeat;
};

class BossEncounterLibrary {
public:
    static BossEncounterLibrary& instance() { static BossEncounterLibrary l; l.init(); return l; }
    const std::vector<BossEncounter>& all() const { return list_; }
    const BossEncounter* find(const std::string& id) const {
        for (auto& e : list_) if (e.id == id) return &e;
        return nullptr;
    }

private:
    void init() {
        if (!list_.empty()) return;

        // --- ASH TYRANT (biome: ashen_wastes) ---------------------------------
        BossEncounter ash;
        ash.id = "ash_tyrant";
        ash.displayName = "Tyrant of Ashes";
        ash.subtitle = "Burned King of the Cinder Plains";
        ash.biomeId = "ashen_wastes";
        ash.arenaCenter = { 0, 0, 0 };
        ash.arenaRadius = 42.f;
        ash.nameplateColor = "#F97316";
        ash.bossPortrait = "ui/portraits/boss_ash_tyrant.png";
        ash.lootCache = "cache_ash_tyrant";
        ash.zoneWideAnnouncement = false;
        ash.cinematic = {
            "anim_cam_ash_intro", "music_ash_tyrant_intro", "vo_ash_tyrant_intro",
            { "The cinders remember every traitor.",
              "And you...",
              "are very, very late." },
            5.f
        };
        // 8 obsidian pillars around the arena
        for (int i = 0; i < 8; ++i) {
            float a = (i / 8.f) * 6.2831853f;
            ash.props.push_back({
                { std::cos(a) * 30.f, 0.f, std::sin(a) * 30.f },
                "arena_obsidian_pillar", a, 1.2f, false, {1,0.4f,0.1f}, 14.f
            });
        }
        // Soul braziers as light sources
        for (int i = 0; i < 4; ++i) {
            float a = (i / 4.f) * 6.2831853f + 0.4f;
            ash.props.push_back({
                { std::cos(a) * 18.f, 0.4f, std::sin(a) * 18.f },
                "soul_brazier", 0.f, 1.f, true, {1.f, 0.55f, 0.18f}, 18.f
            });
        }
        ash.phase1 = {
            { "ash_meteor",   "Meteor Rain",   "Telegraphed AoE meteors fall in 3 rings.",      1.5f, 9.f,  "ring_red",      "explosion_fire", "sfx_warn_meteor",   "sfx_impact_meteor", 90.f, 4.5f },
            { "ash_cone",     "Tyrant's Roar", "Frontal cone of flame; 60° arc, 18m range.",    1.2f, 6.f,  "cone_orange",   "flame_cone",     "sfx_warn_cone",     "sfx_impact_flame",  70.f, 18.f },
            { "ash_smash",    "Cinder Smash",  "Ground slam — knockup in 8m radius.",           0.9f, 7.f,  "ring_yellow",   "ground_smash",   "sfx_warn_smash",    "sfx_impact_smash",  60.f, 8.f  },
        };
        ash.phase2 = {
            { "ash_inferno",  "Inferno Mode",   "Becomes a flame elemental; AoE pulse every 3s.",0.0f, 3.f,  "aura_red",      "aura_pulse_fire","sfx_phase_transition","sfx_pulse_fire",   55.f, 12.f },
            { "ash_pillars",  "Pillars of Wrath","Six pillars erupt from the floor.",            1.8f, 12.f, "spear_orange",  "pillar_eruption","sfx_warn_pillars",  "sfx_impact_pillar", 120.f, 3.5f },
            { "ash_charge",   "Burning Charge", "Charges across the arena 3 times.",             1.0f, 8.f,  "line_orange",   "charge_trail",   "sfx_warn_charge",   "sfx_impact_charge", 90.f, 4.0f },
        };
        list_.push_back(std::move(ash));

        // --- DROWNED KING (biome: sunken_crypts) ------------------------------
        BossEncounter dk;
        dk.id = "drowned_king";
        dk.displayName = "Drowned King";
        dk.subtitle = "Sovereign of the Sunken Crowns";
        dk.biomeId = "sunken_crypts";
        dk.arenaCenter = { 0, 0, 0 };
        dk.arenaRadius = 38.f;
        dk.nameplateColor = "#0D9488";
        dk.bossPortrait = "ui/portraits/boss_drowned_king.png";
        dk.lootCache = "cache_drowned";
        dk.cinematic = {
            "anim_cam_drowned_intro", "music_drowned_intro", "vo_drowned_intro",
            { "I have waited so long beneath the silt.",
              "And you bring me... breath." },
            5.f
        };
        // Coral arches + glowing seaweed
        for (int i = 0; i < 6; ++i) {
            float a = (i / 6.f) * 6.2831853f;
            dk.props.push_back({ { std::cos(a) * 28.f, 0.f, std::sin(a) * 28.f },
                                 "coral_arch", a, 1.3f, false, {0.2f,1.f,0.9f}, 16.f });
            dk.props.push_back({ { std::cos(a + 0.4f) * 24.f, 0.6f, std::sin(a + 0.4f) * 24.f },
                                 "glowing_seaweed", 0.f, 1.f, true, {0.1f, 0.95f, 0.85f}, 14.f });
        }
        dk.phase1 = {
            { "dk_geyser",   "Geyser Burst", "Geysers erupt in line patterns.",                1.4f, 8.f, "line_blue", "water_geyser", "sfx_warn_geyser", "sfx_impact_geyser", 75.f, 3.0f },
            { "dk_tide",     "Receding Tide","Water recedes; players take dmg in low ground.", 2.5f,15.f, "wave_blue", "tide_recede", "sfx_warn_tide",   "sfx_impact_tide",   55.f, 30.f },
            { "dk_grasp",    "Drowned Grasp","Tentacles emerge to root players.",              1.0f, 6.f, "circle_teal","tentacle",    "sfx_warn_grasp",  "sfx_impact_grasp",  35.f, 1.8f },
        };
        dk.phase2 = {
            { "dk_doppels",  "Doppelgangers","Spawns 2 clones — only the original drops loot.",0.f, 0.f,  "aura_green","clone_spawn", "sfx_phase_transition","",         0.f, 0.f },
            { "dk_maelstrom","Maelstrom",    "Whirlpool pulls all players to center.",         3.f,18.f,  "spiral_blue","maelstrom",  "sfx_warn_pull",   "sfx_impact_maelstrom",95.f,40.f },
        };
        list_.push_back(std::move(dk));

        // --- SKY SOVEREIGN (biome: sky_citadel) -------------------------------
        BossEncounter sk;
        sk.id = "sky_sovereign";
        sk.displayName = "Sky Sovereign";
        sk.subtitle = "Crown of the Storm Pantheon";
        sk.biomeId = "sky_citadel";
        sk.arenaCenter = { 0, 0, 0 };
        sk.arenaRadius = 48.f;
        sk.nameplateColor = "#FACC15";
        sk.bossPortrait = "ui/portraits/boss_sky_sovereign.png";
        sk.lootCache = "cache_sky";
        sk.zoneWideAnnouncement = true;
        sk.cinematic = {
            "anim_cam_sky_intro", "music_sky_intro", "vo_sky_intro",
            { "I am the light that burned before the world.",
              "I am the storm that drowned the first kings.",
              "Kneel — or be unmade with their shadows." },
            7.f
        };
        for (int i = 0; i < 12; ++i) {
            float a = (i / 12.f) * 6.2831853f;
            sk.props.push_back({ { std::cos(a) * 36.f, 0.f, std::sin(a) * 36.f },
                                 "marble_pillar", a, 1.6f, false, {1,1,0.7f}, 18.f });
        }
        for (int i = 0; i < 6; ++i) {
            float a = (i / 6.f) * 6.2831853f;
            sk.props.push_back({ { std::cos(a) * 22.f, 0.5f, std::sin(a) * 22.f },
                                 "gold_brazier", 0.f, 1.2f, true, {1.f, 0.9f, 0.4f}, 22.f });
        }
        sk.phase1 = {
            { "sk_lightning", "Chain Lightning",  "Forks between players. Spread out!", 1.3f, 7.f, "line_yellow","lightning",  "sfx_warn_chain",   "sfx_impact_chain",   95.f, 30.f },
            { "sk_wind",      "Sovereign Gust",   "Winds push players toward edges.",   2.0f,10.f, "wave_white", "wind_blast", "sfx_warn_wind",    "sfx_impact_wind",    65.f, 12.f },
            { "sk_judgement", "Divine Judgement", "Pillar of light strikes 3 spots.",   1.5f, 9.f, "circle_gold","light_pillar","sfx_warn_judge",  "sfx_impact_judge",  140.f, 5.0f },
        };
        sk.phase2 = {
            { "sk_ascend",    "Ascension",        "Boss leaps to sky; arena is hailed by judgement.",0.0f,0.f, "aura_gold","ascend_burst","sfx_phase_transition","",      0.f, 0.f },
            { "sk_rain",      "Storm of Pillars", "Light pillars rain on every player marker.",     1.2f, 4.f, "circle_yellow","light_pillar","sfx_warn_rain","sfx_impact_pillar",120.f,4.f },
            { "sk_unmaking",  "Unmaking",         "Channeled 4s wipe — interrupt or die.",          4.0f,40.f, "ring_white","unmake_burst","sfx_warn_unmake","sfx_impact_unmake",9999.f,80.f },
        };
        list_.push_back(std::move(sk));
    }

    std::vector<BossEncounter> list_;
};

// Runtime driver — pumps the encounter's mechanics by phase + cooldown.
class BossEncounterDriver {
public:
    void start(const BossEncounter& enc) {
        encounter_ = &enc;
        phase_ = 1;
        intro_ = enc.cinematic.letterboxSeconds;
        for (auto& m : enc.phase1) cooldowns_[m.id] = 1.5f;
        for (auto& m : enc.phase2) cooldowns_[m.id] = 1.5f;
    }

    void transitionPhase2() { phase_ = 2; }

    // Returns the mechanic that should be cast this frame (or nullptr).
    const BossMechanic* tick(float dt) {
        if (!encounter_) return nullptr;
        if (intro_ > 0) { intro_ -= dt; return nullptr; }
        auto& list = (phase_ == 1) ? encounter_->phase1 : encounter_->phase2;
        for (auto& m : list) {
            auto& cd = cooldowns_[m.id];
            cd -= dt;
            if (cd <= 0) { cd = m.cooldown; return &m; }
        }
        return nullptr;
    }

    int phase() const { return phase_; }
    bool inIntro() const { return intro_ > 0; }

private:
    const BossEncounter* encounter_ = nullptr;
    int phase_ = 1;
    float intro_ = 0.f;
    std::unordered_map<std::string, float> cooldowns_;
};

} // namespace dionite::combat
