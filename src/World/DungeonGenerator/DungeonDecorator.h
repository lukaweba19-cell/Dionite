// ============================================================================
// Dionite — Dungeon Atmosphere: ambient props, fog volumes, blood pools,
// banners, animated torches that turn a tile-built dungeon into something
// that feels handcrafted. Layered on top of DungeonMeshBuilder output.
// ============================================================================
#pragma once
#include "DungeonGenerator.h"
#include "DungeonMeshBuilder.h"
#include "Core/Math/Random.h"
#include "World/Biomes/Biome.h"
#include <vector>
#include <string>

namespace dionite::world {

struct FogVolume {
    math::Vec3 center;
    float radius;
    float density;
    std::string colorHex;
};

struct AtmosphereInstance {
    math::Vec3 position;
    float yaw;
    float scale;
    std::string assetId;
};

struct DungeonAtmosphere {
    std::vector<AtmosphereInstance> banners;
    std::vector<AtmosphereInstance> bloodPools;
    std::vector<AtmosphereInstance> debris;
    std::vector<AtmosphereInstance> hangingChains;
    std::vector<AtmosphereInstance> torches;          // distinct from mesh torches
    std::vector<FogVolume> fogVolumes;
    std::string ambientLoopSfx;                       // "amb_crypt", "amb_forge", "amb_temple"
    float baseFogDensity = 0.012f;
    std::string fogColorHex;
};

class DungeonDecorator {
public:
    DungeonDecorator(BiomeId biome, uint64_t seed) : biome_(biome), rng_(seed) { setBiomeStyle(); }

    DungeonAtmosphere decorate(const Dungeon& dungeon, const DungeonMesh& mesh) {
        DungeonAtmosphere out;
        out.ambientLoopSfx = ambientLoopSfx_;
        out.baseFogDensity = baseFogDensity_;
        out.fogColorHex    = fogColorHex_;

        for (const auto& r : dungeon.rooms) {
            decorateRoom(r, out);
        }
        // Scatter floor debris based on floor count
        for (const auto& f : mesh.floors) {
            if (!rng_.chance(0.025f)) continue;
            out.debris.push_back({ f.worldPos, rng_.rangeF(0.f, 6.28f), rng_.rangeF(0.85f, 1.15f),
                                    debrisAssets_[rng_.range(0, (int)debrisAssets_.size() - 1)] });
        }
        // Blood pools scattered around boss & treasure rooms
        for (const auto& r : dungeon.rooms) {
            if (!r.isBoss && !r.isTreasure) continue;
            int pools = rng_.range(2, 5);
            for (int i = 0; i < pools; ++i) {
                math::Vec3 p{
                    (float)(r.x + rng_.range(1, r.w - 1)) * 2.f,
                    0.02f,
                    (float)(r.y + rng_.range(1, r.h - 1)) * 2.f
                };
                out.bloodPools.push_back({ p, rng_.rangeF(0.f, 6.28f), rng_.rangeF(0.7f, 1.4f),
                                           "decal_blood_pool" });
            }
        }
        return out;
    }

private:
    void decorateRoom(const Room& r, DungeonAtmosphere& out) {
        // Banners on long walls
        if (r.w >= 8) {
            for (int x = r.x + 2; x < r.x + r.w - 2; x += 4) {
                if (!rng_.chance(0.35f)) continue;
                out.banners.push_back({
                    { (float)x * 2.f, 2.2f, (float)r.y * 2.f },
                    0.f, rng_.rangeF(0.95f, 1.10f),
                    bannerAssets_[rng_.range(0, (int)bannerAssets_.size() - 1)]
                });
            }
        }
        // Hanging chains in tall rooms (boss rooms feel heavier)
        if (r.isBoss) {
            for (int i = 0; i < 8; ++i) {
                float a = (i / 8.f) * 6.28f;
                out.hangingChains.push_back({
                    { (float)r.cx() * 2.f + std::cos(a) * 6.f, 3.f, (float)r.cy() * 2.f + std::sin(a) * 6.f },
                    a, rng_.rangeF(0.8f, 1.2f), "chain_hanging"
                });
            }
            // Heavy fog in boss room for dramatic intro
            out.fogVolumes.push_back({
                { (float)r.cx() * 2.f, 1.2f, (float)r.cy() * 2.f },
                std::max((float)r.w, (float)r.h) * 1.6f,
                bossFogDensity_,
                bossFogColorHex_
            });
        }
        // Wall torches inside rooms (different from the corner torches added by builder)
        int desiredTorches = rng_.range(1, 3);
        for (int i = 0; i < desiredTorches; ++i) {
            int tx = rng_.range(r.x + 1, r.x + r.w - 2);
            int ty = rng_.range(r.y + 1, r.y + r.h - 2);
            out.torches.push_back({
                { (float)tx * 2.f, 2.0f, (float)ty * 2.f },
                rng_.rangeF(0.f, 6.28f), 1.f,
                torchAsset_
            });
        }
    }

    void setBiomeStyle() {
        switch (biome_) {
            case BiomeId::VerdantWilds:
                ambientLoopSfx_ = "amb_forest_dungeon";
                baseFogDensity_ = 0.010f; fogColorHex_ = "#0a1a10";
                bossFogDensity_ = 0.025f; bossFogColorHex_ = "#0c2818";
                torchAsset_ = "torch_iron_green";
                bannerAssets_ = {"banner_verdant_a", "banner_verdant_b"};
                debrisAssets_ = {"debris_root", "debris_leaves", "debris_stone_mossy"};
                break;
            case BiomeId::AshenWastes:
                ambientLoopSfx_ = "amb_forge";
                baseFogDensity_ = 0.020f; fogColorHex_ = "#1a0d05";
                bossFogDensity_ = 0.038f; bossFogColorHex_ = "#2a0a05";
                torchAsset_ = "torch_iron_red";
                bannerAssets_ = {"banner_ashen_a", "banner_ashen_b"};
                debrisAssets_ = {"debris_ash", "debris_bone", "debris_charred_wood"};
                break;
            case BiomeId::FrozenSpire:
                ambientLoopSfx_ = "amb_icy_keep";
                baseFogDensity_ = 0.015f; fogColorHex_ = "#08121f";
                bossFogDensity_ = 0.030f; bossFogColorHex_ = "#0a1a30";
                torchAsset_ = "torch_blue_flame";
                bannerAssets_ = {"banner_frost_a", "banner_frost_b"};
                debrisAssets_ = {"debris_ice_shard", "debris_frozen_helm"};
                break;
            case BiomeId::SunkenCrypts:
                ambientLoopSfx_ = "amb_drown";
                baseFogDensity_ = 0.022f; fogColorHex_ = "#061d22";
                bossFogDensity_ = 0.040f; bossFogColorHex_ = "#0a2a32";
                torchAsset_ = "torch_bioluminescent";
                bannerAssets_ = {"banner_drown_a", "banner_drown_b"};
                debrisAssets_ = {"debris_bone_coral", "debris_kelp", "debris_skull"};
                break;
            case BiomeId::SkyCitadel:
                ambientLoopSfx_ = "amb_temple_wind";
                baseFogDensity_ = 0.012f; fogColorHex_ = "#1a1208";
                bossFogDensity_ = 0.022f; bossFogColorHex_ = "#2a1a10";
                torchAsset_ = "torch_gold_flame";
                bannerAssets_ = {"banner_sky_a", "banner_sky_b"};
                debrisAssets_ = {"debris_gold_shard", "debris_broken_marble"};
                break;
        }
    }

    BiomeId biome_;
    math::Random rng_;
    std::string ambientLoopSfx_;
    float baseFogDensity_ = 0.012f;
    float bossFogDensity_ = 0.025f;
    std::string fogColorHex_, bossFogColorHex_, torchAsset_;
    std::vector<std::string> bannerAssets_, debrisAssets_;
};

} // namespace dionite::world
