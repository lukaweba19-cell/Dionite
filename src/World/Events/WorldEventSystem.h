// ============================================================================
// Dionite — World Events (Helltides, Blood Moon, Treasure Goblin, Worldboss).
// Triggered by EventScheduler. Each event installs custom spawn rules,
// timed lootcrate placements, biome tint overrides, and SFX/music.
// ============================================================================
#pragma once
#include "Core/Math/Vector.h"
#include "Core/Math/Random.h"
#include "World/Biomes/Biome.h"
#include <string>
#include <vector>
#include <functional>

namespace dionite::world {

enum class WorldEventKind : uint8_t {
    Helltide,      // 60-min: all biomes turn red, enemies drop Aberrant Cinders
    BloodMoon,     // 10-min: night sky, +200% enemy spawn, vampiric enemies
    TreasureGoblin,// 90-sec: a goblin spawns; loot only when killed
    LegionEvent,   // 5-min: scripted multi-wave defense
    WorldBoss,     // 15-min: zone-wide boss with global notification
    Whisper,       // 30-min: tracker quest in a random biome
};

struct WorldEvent {
    std::string id;
    std::string title;
    std::string description;
    WorldEventKind kind;
    BiomeId       biome;
    math::Vec3    epicenter;
    float         durationSec;
    float         elapsed = 0.f;
    bool          active  = false;

    // Visual overrides
    std::string fogColorHex;     // pushes into TerrainCameraUniforms.fogColor
    std::string ambientHex;
    std::string sunColorHex;
    std::string musicId;
    float       postBloomMult = 1.f;
    float       postSaturation = 1.f;
    float       postFogDensity = 0.01f;

    // Reward overrides
    float lootBoost = 1.f;
    int   guaranteedLegendary = 0;
    std::string specialCurrency; // "aberrant_cinders", "legion_marks", etc.
    int   specialCurrencyDrop = 0;

    // Hooks
    std::function<void()> onStart;
    std::function<void(float dt)> onTick;
    std::function<void()> onEnd;
};

class WorldEventSystem {
public:
    explicit WorldEventSystem(uint64_t seed) : rng_(seed) {}

    static WorldEvent makeHelltide(BiomeId biome, math::Vec3 epicenter) {
        WorldEvent e;
        e.id = "helltide";
        e.title = "Helltide — The Veil Burns";
        e.description = "For one hour, this region bleeds. Slain foes drop Aberrant Cinders. Tortured Gifts spawn at dusk shrines.";
        e.kind = WorldEventKind::Helltide;
        e.biome = biome;
        e.epicenter = epicenter;
        e.durationSec = 3600.f;
        e.fogColorHex = "#7f1d1d";
        e.ambientHex  = "#dc2626";
        e.sunColorHex = "#fb923c";
        e.musicId = "music_helltide_drone";
        e.postBloomMult = 1.4f;
        e.postSaturation = 1.15f;
        e.postFogDensity = 0.022f;
        e.lootBoost = 1.75f;
        e.guaranteedLegendary = 1;
        e.specialCurrency = "aberrant_cinders";
        e.specialCurrencyDrop = 12;
        return e;
    }

    static WorldEvent makeBloodMoon(BiomeId biome, math::Vec3 epicenter) {
        WorldEvent e;
        e.id = "blood_moon";
        e.title = "Blood Moon Rising";
        e.description = "Vampiric foes prowl. Killing them restores health. Survive 10 minutes for a Mythic-tier cache.";
        e.kind = WorldEventKind::BloodMoon;
        e.biome = biome;
        e.epicenter = epicenter;
        e.durationSec = 600.f;
        e.fogColorHex = "#450a0a";
        e.ambientHex  = "#7f1d1d";
        e.sunColorHex = "#fde047";
        e.musicId = "music_blood_moon";
        e.postBloomMult = 1.6f;
        e.postFogDensity = 0.03f;
        e.lootBoost = 1.5f;
        e.guaranteedLegendary = 1;
        return e;
    }

    static WorldEvent makeTreasureGoblin(BiomeId biome, math::Vec3 epicenter) {
        WorldEvent e;
        e.id = "treasure_goblin";
        e.title = "Treasure Goblin!";
        e.description = "A laughing imp drags a sack of gold. Catch it before it escapes through a Greed Portal.";
        e.kind = WorldEventKind::TreasureGoblin;
        e.biome = biome;
        e.epicenter = epicenter;
        e.durationSec = 90.f;
        e.musicId = "music_goblin_chase";
        e.postBloomMult = 1.2f;
        e.postSaturation = 1.2f;
        e.lootBoost = 4.0f;
        e.guaranteedLegendary = 2;
        return e;
    }

    static WorldEvent makeWorldBoss(BiomeId biome, math::Vec3 epicenter, const std::string& bossId) {
        WorldEvent e;
        e.id = "worldboss_" + bossId;
        e.title = "World Boss: " + bossId;
        e.description = "A colossal entity tears the veil. All players in the zone are summoned. 15 minutes to break it.";
        e.kind = WorldEventKind::WorldBoss;
        e.biome = biome;
        e.epicenter = epicenter;
        e.durationSec = 900.f;
        e.fogColorHex = "#1f2937";
        e.musicId = "music_worldboss";
        e.postBloomMult = 1.5f;
        e.postFogDensity = 0.025f;
        e.lootBoost = 2.5f;
        e.guaranteedLegendary = 2;
        return e;
    }

    void schedule(WorldEvent e) {
        e.active = true;
        if (e.onStart) e.onStart();
        active_.push_back(std::move(e));
    }

    void update(float dt) {
        for (auto& e : active_) {
            if (!e.active) continue;
            e.elapsed += dt;
            if (e.onTick) e.onTick(dt);
            if (e.elapsed >= e.durationSec) { if (e.onEnd) e.onEnd(); e.active = false; }
        }
        active_.erase(std::remove_if(active_.begin(), active_.end(),
            [](const WorldEvent& e) { return !e.active; }), active_.end());
    }

    const std::vector<WorldEvent>& events() const { return active_; }

private:
    math::Random rng_;
    std::vector<WorldEvent> active_;
};

} // namespace dionite::world
