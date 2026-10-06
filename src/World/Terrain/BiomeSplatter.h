// ============================================================================
// Dionite — World: Biome splat map.
// Produces a per-vertex 4-channel weight (Grass | Rock | Dirt | Snow) that
// the terrain shader samples to blend the four PBR material layers.
// Splatting rule:
//   - Snow at high altitude
//   - Rock at high slope
//   - Dirt around water-line + scattered patches
//   - Grass everywhere else
// Each biome overrides the recipe with its own palette + thresholds.
// ============================================================================
#pragma once
#include "Heightmap.h"
#include "TerrainMeshBuilder.h"
#include "Noise.h"
#include "World/Biomes/Biome.h"
#include <vector>

namespace dionite::world {

struct SplatWeights { float grass, rock, dirt, snow; };

struct BiomeSplatRecipe {
    float snowAltitude  = 55.f;  // above this is snow capable
    float rockSlope     = 0.55f; // above this slope is rock
    float dirtNearWater = 6.f;   // below this altitude blends dirt
    float grassPatchNoise = 1.f / 24.f;
    float dirtPatchNoise  = 1.f / 14.f;
    std::string grassMatId = "mat_grass_verdant";
    std::string rockMatId  = "mat_rock_verdant";
    std::string dirtMatId  = "mat_dirt_verdant";
    std::string snowMatId  = "mat_snow_verdant";
};

class BiomeSplatter {
public:
    BiomeSplatter(BiomeId biomeId, uint64_t seed)
        : biomeId_(biomeId), noise_(seed), recipe_(recipeFor(biomeId)) {}

    std::vector<SplatWeights> compute(const Heightmap& h, const TerrainTile& tile) const {
        std::vector<SplatWeights> out;
        out.reserve(tile.mesh.vertices.size());
        for (size_t i = 0; i < tile.mesh.vertices.size(); ++i) {
            const auto& v  = tile.mesh.vertices[i];
            float slope    = tile.slope[i];
            float altitude = tile.altitude[i];
            out.push_back(sample(v.position.x, v.position.z, altitude, slope, h.maxHeight));
        }
        return out;
    }

    const BiomeSplatRecipe& recipe() const { return recipe_; }

private:
    SplatWeights sample(float wx, float wz, float altitude, float slope, float maxH) const {
        SplatWeights w{0, 0, 0, 0};
        float snowMix = smoothstep01((altitude - recipe_.snowAltitude) / 12.f);
        float rockMix = smoothstep01((slope - recipe_.rockSlope)        / 0.15f);
        float dirtPatch = noise_.value(wx * recipe_.dirtPatchNoise, wz * recipe_.dirtPatchNoise) * 0.5f + 0.5f;
        float dirtMix = std::max(0.f, recipe_.dirtNearWater - altitude) * 0.1f + std::max(0.f, dirtPatch - 0.65f) * 1.4f;
        dirtMix = std::min(1.f, dirtMix);
        float grassPatch = noise_.value(wx * recipe_.grassPatchNoise, wz * recipe_.grassPatchNoise) * 0.5f + 0.5f;
        float grass = std::max(0.f, grassPatch);

        // Normalise (rock > snow > dirt > grass priority)
        w.rock = rockMix;
        w.snow = (1.f - w.rock) * snowMix;
        w.dirt = (1.f - w.rock - w.snow) * dirtMix;
        w.grass = std::max(0.f, 1.f - w.rock - w.snow - w.dirt) * grass;
        // Add a tiny residual grass to never have a black pixel
        w.grass += 1e-3f;
        float s = w.grass + w.rock + w.dirt + w.snow;
        w.grass /= s; w.rock /= s; w.dirt /= s; w.snow /= s;
        return w;
    }

    static float smoothstep01(float t) {
        t = std::max(0.f, std::min(1.f, t));
        return t * t * (3.f - 2.f * t);
    }

    static BiomeSplatRecipe recipeFor(BiomeId id) {
        BiomeSplatRecipe r;
        switch (id) {
            case BiomeId::VerdantWilds:
                r.snowAltitude=70.f; r.rockSlope=0.55f;
                r.grassMatId="mat_grass_verdant"; r.rockMatId="mat_rock_verdant";
                r.dirtMatId="mat_dirt_verdant"; r.snowMatId="mat_snow_generic"; break;
            case BiomeId::AshenWastes:
                r.snowAltitude=200.f; r.rockSlope=0.45f; r.dirtNearWater=10.f;
                r.grassMatId="mat_ash_floor"; r.rockMatId="mat_rock_ashen";
                r.dirtMatId="mat_cinder";    r.snowMatId="mat_soot"; break;
            case BiomeId::FrozenSpire:
                r.snowAltitude=20.f; r.rockSlope=0.6f;
                r.grassMatId="mat_tundra"; r.rockMatId="mat_rock_frozen";
                r.dirtMatId="mat_ice_dirt"; r.snowMatId="mat_snow_pure"; break;
            case BiomeId::SunkenCrypts:
                r.snowAltitude=999.f; r.rockSlope=0.5f; r.dirtNearWater=14.f;
                r.grassMatId="mat_moss_sunken"; r.rockMatId="mat_rock_sunken";
                r.dirtMatId="mat_silt";    r.snowMatId="mat_corral"; break;
            case BiomeId::SkyCitadel:
                r.snowAltitude=80.f; r.rockSlope=0.5f;
                r.grassMatId="mat_skygrass"; r.rockMatId="mat_marble";
                r.dirtMatId="mat_goldsand";  r.snowMatId="mat_cloudstone"; break;
        }
        return r;
    }

    BiomeId biomeId_;
    Noise noise_;
    BiomeSplatRecipe recipe_;
};

} // namespace dionite::world
