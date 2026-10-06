// ============================================================================
// Dionite — GameRuntime implementation.
//
// Owns one full play session: boots a campaign (or continues a save), builds a
// procedural dungeon for the current region/stage, spawns enemies and a boss,
// runs combat / loot / quests / progression / endgame every tick, and finally
// flattens the world into the pure-C snapshot consumed by the platform
// renderer (Swift + Metal on iOS).
// ============================================================================
#include "Game/GameRuntime.h"

#include "Progression/Skills/SkillLibrary.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace dionite::game {
namespace {

// ---------------------------------------------------------------------------
// Colour helpers — DIInstance::rgba is byte-order R,G,B,A in memory.
// ---------------------------------------------------------------------------
constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
}
constexpr uint8_t ch(uint32_t c, int i) { return (uint8_t)((c >> (i * 8)) & 0xFFu); }
constexpr uint32_t withAlpha(uint32_t c, uint8_t a) {
    return (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
}
uint32_t shade(uint32_t c, float k) {
    auto cl = [](float v) { return (uint8_t)std::max(0.f, std::min(255.f, v)); };
    return rgb(cl(ch(c, 0) * k), cl(ch(c, 1) * k), cl(ch(c, 2) * k));
}
void unpackRgb(uint32_t c, float out[3]) {
    out[0] = ch(c, 0) / 255.f;
    out[1] = ch(c, 1) / 255.f;
    out[2] = ch(c, 2) / 255.f;
}
// "#rrggbb" -> packed rgb (alpha 0xFF)
uint32_t parseHex(const std::string& s, uint32_t fallback = 0xFFFFFFFFu) {
    if (s.size() < 7 || s[0] != '#') return fallback;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    uint32_t r = (uint32_t)(nib(s[1]) * 16 + nib(s[2]));
    uint32_t g = (uint32_t)(nib(s[3]) * 16 + nib(s[4]));
    uint32_t b = (uint32_t)(nib(s[5]) * 16 + nib(s[6]));
    return rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

DIInstance mkInst(float x, float y, float z, float rotY,
                  float sx, float sy, float sz,
                  uint32_t rgba, float emissive, uint32_t mesh, uint32_t flags,
                  float phase = 0.f) {
    DIInstance in{};
    in.x = x; in.y = y; in.z = z;
    in.rotY = rotY;
    in.sx = sx; in.sy = sy; in.sz = sz;
    in.emissive = emissive;
    in.rgba = rgba;
    in.mesh = mesh;
    in.flags = flags;
    in.phase = phase;
    return in;
}

// ---------------------------------------------------------------------------
// Region / art direction (see design_guidelines.json -> color_palette.biomes)
// ---------------------------------------------------------------------------
struct RegionArt {
    const char* id;
    const char* name;
    uint32_t floorA, floorB, wall, trim, accent, fog;
    float fogDensity, ambient, sunIntensity;
    uint32_t sunColor;
    uint32_t enemyColor[4];
    const char* mob[4];
    const char* boss;
    const char* bossTitle;
};

constexpr RegionArt kRegions[5] = {
    { // 0 — Verdant Wilds
        "verdant_wilds", "Verdant Wilds",
        rgb(20, 60, 44), rgb(15, 52, 38), rgb(30, 44, 36), rgb(48, 76, 58),
        rgb(52, 211, 153), rgb(7, 22, 15),
        0.021f, 0.44f, 1.15f, rgb(255, 243, 214),
        { rgb(126, 160, 120), rgb(88, 110, 84), rgb(152, 172, 122), rgb(62, 146, 112) },
        { "Ghoul", "Dire Wolf", "Rot Archer", "Mire Stalker" },
        "Mossheart", "Verdant Tyrant"
    },
    { // 1 — Ashen Wastes
        "ashen_wastes", "Ashen Wastes",
        rgb(44, 27, 19), rgb(37, 23, 18), rgb(48, 34, 28), rgb(74, 52, 39),
        rgb(249, 115, 22), rgb(23, 11, 6),
        0.026f, 0.37f, 1.05f, rgb(255, 211, 168),
        { rgb(152, 122, 100), rgb(202, 92, 52), rgb(232, 142, 82), rgb(122, 92, 80) },
        { "Ash Zombie", "Cinder Hound", "Ember Archer", "Slag Brute" },
        "The Cinder King", "Lord of Cinders"
    },
    { // 2 — Sunken Crypts
        "sunken_crypts", "Sunken Crypts",
        rgb(13, 44, 48), rgb(10, 37, 40), rgb(19, 52, 58), rgb(30, 72, 78),
        rgb(45, 212, 191), rgb(4, 20, 22),
        0.033f, 0.31f, 0.82f, rgb(191, 246, 238),
        { rgb(92, 152, 152), rgb(72, 182, 162), rgb(202, 207, 192), rgb(52, 122, 132) },
        { "Drowned One", "Crypt Leech", "Bone Archer", "Mire Warden" },
        "Vashj", "Drowned Oracle"
    },
    { // 3 — Frozen Spire
        "frozen_spire", "Frozen Spire",
        rgb(28, 44, 66), rgb(23, 37, 57), rgb(37, 53, 74), rgb(58, 78, 106),
        rgb(56, 189, 248), rgb(10, 18, 32),
        0.020f, 0.49f, 1.30f, rgb(232, 246, 255),
        { rgb(172, 216, 240), rgb(122, 166, 202), rgb(202, 232, 246), rgb(142, 202, 232) },
        { "Frost Wraith", "Ice Troll", "Winter Archer", "Rime Hound" },
        "Rimefang", "The Devourer"
    },
    { // 4 — Sky Citadel
        "sky_citadel", "Sky Citadel",
        rgb(76, 60, 38), rgb(69, 55, 34), rgb(92, 75, 50), rgb(122, 102, 68),
        rgb(253, 224, 71), rgb(44, 35, 20),
        0.016f, 0.56f, 1.40f, rgb(255, 248, 225),
        { rgb(242, 232, 202), rgb(152, 152, 167), rgb(247, 217, 142), rgb(212, 207, 192) },
        { "Fallen Seraph", "Storm Golem", "Sun Zealot", "Cloud Stalker" },
        "Aurelius", "Last Seraph"
    },
};

// Archetypes: grunt / brute / ranged / stalker
struct Archetype { bool ranged; float hpMul, dmgMul, speed, size, attackRange; };
constexpr Archetype kArch[4] = {
    { false, 1.00f, 1.00f, 3.6f, 1.00f, 2.1f },
    { false, 2.40f, 1.60f, 2.6f, 1.45f, 2.7f },
    { true,  0.70f, 0.90f, 3.0f, 0.95f, 14.f },
    { false, 1.35f, 1.20f, 5.2f, 1.05f, 2.3f },
};
// which region name/colour slot each archetype wears
constexpr int kArchSkin[4] = { 0, 3, 2, 1 };

const char* kClassWeapon[5] = { "rifle", "frostwand", "staff", "pistol", "shotgun" };
const uint32_t kClassColor[5] = {
    rgb(206, 166, 86), rgb(156, 94, 208), rgb(84, 144, 232), rgb(74, 174, 114), rgb(94, 194, 224)
};
const uint32_t kRarityColor[6] = {
    rgb(176, 176, 176), rgb(59, 130, 246), rgb(251, 191, 36),
    rgb(168, 85, 247), rgb(249, 115, 22), rgb(225, 29, 72)
};

constexpr float kTile = world::DungeonMeshBuilder::TILE;
constexpr float kWallH = world::DungeonMeshBuilder::WALL_HEIGHT;

math::Vec3 tileWorld(int x, int y) {
    return { (float)x * kTile, 0.f, (float)y * kTile };
}

bool isFloorTile(world::Tile t) {
    return t == world::Tile::Floor || t == world::Tile::Spawn ||
           t == world::Tile::Chest || t == world::Tile::BossPad || t == world::Tile::Door;
}

void copyStr(char* dst, size_t n, const std::string& s) {
    std::snprintf(dst, n, "%s", s.c_str());
}

audio::Ambient ambientForRegion(int region) {
    switch (std::max(0, std::min(4, region))) {
        case 0:  return audio::Ambient::Forest;
        case 1:  return audio::Ambient::Ash;
        case 2:  return audio::Ambient::Crypt;
        case 3:  return audio::Ambient::Ice;
        default: return audio::Ambient::Sky;
    }
}

} // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================
void GameRuntime::boot(const std::string& saveDir, int classId, uint64_t seed, bool hasSave) {
    saveDir_ = saveDir;
    classId_ = std::max(0, std::min(4, classId));
    campaignSeed_ = seed ? seed : 0x5EED1234A7ULL;

    bool restored = false;
    if (hasSave) restored = loadSave();

    classDef_ = progression::ClassRegistry::instance().find((progression::ClassId)classId_);
    buildLoadout();

    if (!restored) {
        region_ = 0; stage_ = 0; unlockedRegions_ = 1;
        level_ = 1; xp_ = 0; gold_ = 0; skillPoints_ = 0; kills_ = 0; potions_ = 3;
        inventory_.clear(); equippedIdx_ = -1;
        playSeconds_ = 0.f;
        buildLoadout();
    }

    loadedSave_ = restored;
    audio_.reset();
    audio_.setAmbient(ambientForRegion(region_));
    setupQuests(region_);
    generateLevel(region_, stage_, false);

    dead_ = false;
    bootDone_ = true;
    autosaveTimer_ = 0.f;
    toast(DI_EVENT_INFO, "%s — %s", classDef_ ? classDef_->displayName.c_str() : "Hero",
          kRegions[region_].name);
}

void GameRuntime::shutdown() {
    if (bootDone_) writeSave();
    bootDone_ = false;
}

void GameRuntime::resize(int width, int height) {
    if (width > 0 && height > 0) levelAspect_ = (float)width / (float)height;
}

void GameRuntime::buildLoadout() {
    const progression::ClassId cid = (progression::ClassId)classId_;
    if (!classDef_) classDef_ = progression::ClassRegistry::instance().find(cid);

    loadout_ = progression::PlayerLoadout{cid};
    auto& lib = progression::SkillLibrary::instance();

    auto pick = [&](int slot, progression::SkillCategory cat, const std::string& preferred) {
        if (!preferred.empty() && lib.find(preferred)) {
            loadoutMgr_.assign(loadout_, slot, preferred);
            return;
        }
        auto list = lib.forClassAndCategory(cid, cat);
        std::sort(list.begin(), list.end(),
                  [](const progression::SkillDefinition* a, const progression::SkillDefinition* b) {
                      return a->id < b->id;
                  });
        if (!list.empty()) loadoutMgr_.assign(loadout_, slot, list.front()->id);
    };

    const std::string start = classDef_ ? classDef_->startingSkillId : std::string();
    pick(0, progression::SkillCategory::Basic,     start);
    pick(1, progression::SkillCategory::Core,      "");
    pick(2, progression::SkillCategory::Defensive, "");
    pick(3, progression::SkillCategory::Mobility,  "");
    pick(4, progression::SkillCategory::Utility,   "");
    pick(5, progression::SkillCategory::Ultimate,  "");

    // Base vitals from the class definition + level scaling.
    const float hp  = classDef_ ? classDef_->baseHealth  + classDef_->healthPerLevel  * (level_ - 1) : 100.f;
    const float res = classDef_ ? classDef_->baseResource + classDef_->resourcePerLevel * (level_ - 1) : 100.f;

    const float hpFrac = player_.stats().maxHealth > 0.f
                       ? player_.stats().health / player_.stats().maxHealth : 1.f;
    player_.stats().maxHealth = hp + level_ * 2.f;
    player_.stats().health = std::max(1.f, player_.stats().maxHealth * (bootDone_ ? hpFrac : 1.f));

    resource_.maximum = res;
    resource_.current = res;
    resource_.regenPerSec = classDef_ ? classDef_->resourceRegen : 0.f;

    player_.stats().critChance = 0.10f + level_ * 0.004f;
    player_.stats().critMult   = 1.65f;
    player_.stats().lifesteal  = 0.02f;

    // Weapon: equipped item overrides the class signature weapon.
    if (equippedIdx_ >= 0 && equippedIdx_ < (int)inventory_.size()) {
        const loot::Item& it = inventory_[(size_t)equippedIdx_];
        weapon_ = combat::WeaponSystem::fromTemplate(it.baseId);
        weapon_.damage = it.damage;
        for (const auto& a : it.affixes) {
            if (a.stat == "dmgPct")     weapon_.damage *= (1.f + a.amount);
            else if (a.stat == "firePct") weapon_.damage *= (1.f + a.amount * 0.5f);
        }
        weapon_.displayName = it.name;
    } else {
        equippedIdx_ = -1;
        weapon_ = combat::WeaponSystem::fromTemplate(
            kClassWeapon[std::max(0, std::min(4, classId_))]);
        weapon_.damage *= (1.f + (level_ - 1) * 0.07f);
    }
    weapon_.currentMagazine = weapon_.magazineSize;
    weapon_.reloading = false;
    weapon_.currentFireCd = 0.f;

    // static cooldown budgets for the HUD (per skill, at current rank)
    for (int i = 0; i < DI_MAX_ABILITIES; ++i) {
        const std::string& id = loadout_.active[(size_t)i].skillId;
        const auto* def = id.empty() ? nullptr : progression::SkillLibrary::instance().find(id);
        abilityCdMax_[i] = def ? def->cooldown : 0.f;
        abilityCd_[i] = std::min(abilityCd_[i], abilityCdMax_[i]);
    }
}

// ===========================================================================
// Level generation
// ===========================================================================
void GameRuntime::generateLevel(int region, int stage, bool spire) {
    region = std::max(0, std::min(4, region));
    region_ = region;
    stage_ = std::max(0, stage);
    uint64_t h = campaignSeed_;
    h = h * 1000003ULL + (uint64_t)(region + 1) * 7919ULL + (uint64_t)stage * 104729ULL;
    if (spire) h ^= 0x9E3779B97F4A7C15ULL;

    const int rooms = 10 + (spire ? 0 : stage) + (region >= 3 ? 2 : 0);
    const int floorNo = 1 + region * 4 + stage;

    dgen_ = std::make_unique<world::DungeonGenerator>(h, 96, 96);
    dungeon_ = dgen_->generate(rooms, floorNo);
    if (spire && !spireRun_.currentAffixes.empty())
        dungeon_.affixes = spireRun_.currentAffixes;

    affixDmg_ = affixHp_ = affixLoot_ = affixSpeed_ = 1.f;
    for (const auto& a : dungeon_.affixes) {
        affixDmg_   *= a.damageMult;
        affixHp_    *= a.enemyHpMult;
        affixLoot_  *= a.lootBoost;
        affixSpeed_ *= a.playerSpeedMult;
    }

    mesh_ = world::DungeonMeshBuilder().build(dungeon_, kRegions[region].id);
    nav_.reset();
    nav_ = std::make_unique<world::NavGrid>(dungeon_, kTile);

    spawnWorld_ = mesh_.spawnWorld;
    buildLevelGeometry(dungeon_, kRegions[region].id);
    spawnEnemies(dungeon_, region, floorNo);
    setupBoss(dungeon_, region, floorNo);

    // chests from the generator's treasure markers
    chestProps_.clear();
    for (const auto& pr : mesh_.props) {
        if (pr.kind != "chest") continue;
        ChestProp cp;
        cp.pos = pr.worldPos + math::Vec3(kTile * 0.5f, 0.f, kTile * 0.5f);
        cp.chest.kind = (stage >= 3 || region >= 3) ? loot::ChestKind::Gilded
                       : (stage >= 1 ? loot::ChestKind::Iron : loot::ChestKind::Wooden);
        if (rng_.chance(0.07f)) cp.chest.kind = loot::ChestKind::Mimic;
        cp.chest.seed = rng_.range(0, 1 << 30);
        cp.chest.opened = false;
        cp.chest.playerLevel = level_;
        chestProps_.push_back(cp);
    }

    // portals
    portal_.alive = false;
    portal_.kind = 0;
    portal_.pos = mesh_.spawnWorld;
    for (int y = 0; y < dungeon_.height; ++y) {
        bool found = false;
        for (int x = 0; x < dungeon_.width; ++x) {
            if (dungeon_.at(x, y) != world::Tile::BossPad) continue;
            portal_.pos = tileWorld(x, y) + math::Vec3(kTile * 0.5f, 0.f, kTile * 0.5f);
            found = true;
            break;
        }
        if (found) break;
    }

    spireExit_.alive = spire;
    spireExit_.kind = 1;
    spireExit_.pos = mesh_.spawnWorld;

    // reset transient world state
    pickups_.clear();
    effects_.clear();
    playerProj_.clear();
    enemyProj_.clear();
    navPath_.clear();
    hasNavGoal_ = false;
    prompt_.clear();
    bossIntroShown_ = false;
    bossPhaseShown_ = -1;
    bossAtkTimer_ = 3.5f;
    eventTimer_ = 120.f + rng_.rangeF(0.f, 60.f);

    // place the player
    player_.setPosition(spawnWorld_);
    player_.clearNavTarget();
    player_.stats().invulnTimer = 1.5f;
    camera_ = player::GameCamera();
    camera_.follow(spawnWorld_, 1.f);
    if (!bootDone_) player_.stats().health = player_.stats().maxHealth;
}

void GameRuntime::spawnEnemies(const world::Dungeon& d, int region, int floorNo) {
    enemies_.clear();
    bossIndex_ = -1;
    boss_.reset();

    region = std::max(0, std::min(4, region));
    const RegionArt& R = kRegions[region];
    const float lv = (float)std::max(1, floorNo);

    auto make = [&](const math::Vec3& pos, int arch, bool elite, const std::string& name) {
        const Archetype& A = kArch[arch];
        Enemy en;
        en.arch = arch;
        en.e.typeId = name;
        en.e.position = pos;
        en.e.position.y = 0.f;
        en.e.stats.ranged = A.ranged;
        en.e.stats.maxHp = 44.f * (1.f + 0.30f * (lv - 1.f)) * A.hpMul * affixHp_;
        if (spireMode_) en.e.stats.maxHp *= spire_.enemyHpMult(spireFloor_);
        en.e.stats.hp = en.e.stats.maxHp;
        en.e.stats.dmg = 7.f * (1.f + 0.17f * (lv - 1.f)) * A.dmgMul * affixDmg_;
        if (spireMode_) en.e.stats.dmg *= spire_.enemyDmgMult(spireFloor_);
        en.e.stats.speed = A.speed * (1.f + lv * 0.012f);
        en.e.stats.aggroRange = A.ranged ? 26.f : 20.f;
        en.e.stats.attackRange = A.attackRange;
        en.e.stats.attackCd = 1.2f;
        en.e.stats.fireRate = A.ranged ? 1.7f : 1.0f;
        en.e.stats.projSpeed = 26.f;
        en.e.stats.state = combat::AIState::Idle;
        en.e.stats.stateTimer = 0.f;
        en.e.fireTimer = rng_.rangeF(0.2f, 1.4f);
        en.e.targetDir = math::Vec3(1, 0, 0);
        en.elite = elite;
        if (elite) {
            en.e.stats.maxHp *= 3.2f;
            en.e.stats.hp = en.e.stats.maxHp;
            en.e.stats.dmg *= 1.7f;
            en.e.stats.speed *= 1.1f;
        }
        en.xp = (12.f + lv * 6.f) * (elite ? 3.f : 1.f) * A.hpMul;
        en.goldMin = 2.f + lv * 1.5f;
        en.goldMax = 6.f + lv * 3.5f;
        en.hitFlash = 0.f;
        en.corpseTimer = 0.f;
        enemies_.push_back(en);
    };

    const int spawnRoom = d.rooms.empty() ? -1 : 0;
    const int bossRoom = d.rooms.empty() ? -1 : (int)d.rooms.size() - 1;

    for (size_t ri = 0; ri < d.rooms.size(); ++ri) {
        if ((int)ri == spawnRoom) continue;
        if ((int)ri == bossRoom) continue;
        const world::Room& r = d.rooms[ri];
        const int count = 2 + rng_.range(0, 2) + (floorNo > 6 ? 1 : 0);
        for (int i = 0; i < count; ++i) {
            int tx = rng_.range(r.x, r.x + r.w - 1);
            int ty = rng_.range(r.y, r.y + r.h - 1);
            if (!nav_ || !nav_->walkable(tx, ty)) continue;
            int arch = rng_.range(0, 3);
            bool elite = rng_.chance(0.09f);
            make(tileWorld(tx, ty) + math::Vec3(kTile * 0.5f, 0.f, kTile * 0.5f),
                 arch, elite, R.mob[kArchSkin[arch]]);
        }
    }

    // Guarantee a minimum population even if the generator produced tiny rooms.
    if (enemies_.empty()) make(spawnWorld_ + math::Vec3(8, 0, 8), 0, false, R.mob[0]);
}

void GameRuntime::setupBoss(const world::Dungeon& d, int region, int floorNo) {
    region = std::max(0, std::min(4, region));
    const RegionArt& R = kRegions[region];

    math::Vec3 pos = mesh_.spawnWorld;
    for (int y = 0; y < d.height; ++y) {
        bool found = false;
        for (int x = 0; x < d.width; ++x) {
            if (d.at(x, y) != world::Tile::BossPad) continue;
            pos = tileWorld(x, y) + math::Vec3(kTile * 0.5f, 0.f, kTile * 0.5f);
            found = true;
            break;
        }
        if (found) break;
    }

    const float lv = (float)std::max(1, floorNo);
    Enemy en;
    en.boss = true;
    en.elite = true;
    en.e.typeId = "boss";
    en.e.position = pos;
    en.e.stats.ranged = false;
    en.e.stats.maxHp = 900.f * (1.f + 0.34f * (lv - 1.f)) * affixHp_;
    if (spireMode_) en.e.stats.maxHp *= spire_.enemyHpMult(spireFloor_);
    en.e.stats.hp = en.e.stats.maxHp;
    en.e.stats.dmg = 22.f * (1.f + 0.20f * (lv - 1.f)) * affixDmg_;
    if (spireMode_) en.e.stats.dmg *= spire_.enemyDmgMult(spireFloor_);
    en.e.stats.speed = 3.2f;
    en.e.stats.aggroRange = 60.f;
    en.e.stats.attackRange = 3.4f;
    en.e.stats.fireRate = 1.4f;
    en.e.stats.state = combat::AIState::Idle;
    en.e.fireTimer = 1.5f;
    en.xp = 260.f + lv * 55.f;
    en.goldMin = 60.f + lv * 12.f;
    en.goldMax = 140.f + lv * 26.f;

    enemies_.push_back(en);
    bossIndex_ = (int)enemies_.size() - 1;

    boss_ = std::make_unique<combat::BossInstance>();
    boss_->base = enemies_[(size_t)bossIndex_].e;
    boss_->name = R.boss;
    boss_->phase = combat::BossPhase::Intro;
    boss_->phaseTimer = 2.4f;
    boss_->patternTimer = 3.0f;
    boss_->currentPattern = 0;
    // Pattern cadence only; the actual mechanics live in updateBoss() so they
    // can spawn telegraphs and projectiles through the runtime.
    boss_->phase1Patterns = {
        { "advance", 4.5f, {} }, { "slam", 4.5f, {} }, { "volley", 4.5f, {} }
    };
    boss_->phase2Patterns = {
        { "advance", 3.2f, {} }, { "slam", 3.2f, {} }, { "volley", 3.2f, {} }, { "charge", 3.2f, {} }
    };
}

// ===========================================================================
// Static level geometry
// ===========================================================================
void GameRuntime::buildLevelGeometry(const world::Dungeon& d, const std::string& biomeMat) {
    (void)biomeMat;
    levelGeom_.clear();
    levelGeomTrans_.clear();
    levelGeomAdd_.clear();
    const RegionArt& R = kRegions[std::max(0, std::min(4, region_))];

    levelGeom_.reserve((size_t)d.width * (size_t)d.height / 2);
    levelGeomAdd_.reserve(128);

    for (int y = 0; y < d.height; ++y) {
        for (int x = 0; x < d.width; ++x) {
            const world::Tile t = d.at(x, y);
            if (t == world::Tile::Void) continue;
            const float wx = (float)x * kTile;
            const float wz = (float)y * kTile;
            const uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
            const float jitter = 0.94f + ((h >> 5) & 15) / 150.f;

            if (isFloorTile(t)) {
                uint32_t c = ((x + y) & 1) ? R.floorA : R.floorB;
                levelGeom_.push_back(mkInst(wx, 0.f, wz, 0.f,
                                            kTile, 1.f, kTile,
                                            shade(c, jitter), 0.f,
                                            DI_MESH_QUAD, 0, ((h >> 9) & 63) * 0.1f));
                if (t == world::Tile::BossPad) {
                    levelGeomAdd_.push_back(mkInst(wx, 0.04f, wz, 0.f,
                                                   kTile * 1.05f, 1.f, kTile * 1.05f,
                                                   withAlpha(R.accent, 120), 1.4f,
                                                   DI_MESH_QUAD, DI_FLAG_UNLIT, 1.f));
                }
                if (t == world::Tile::Spawn) {
                    levelGeomAdd_.push_back(mkInst(wx, 0.03f, wz, 0.f,
                                                   kTile * 1.4f, 1.f, kTile * 1.4f,
                                                   withAlpha(rgb(147, 51, 234), 90), 1.1f,
                                                   DI_MESH_QUAD, DI_FLAG_UNLIT, 0.4f));
                }
                continue;
            }
            if (t == world::Tile::Wall) {
                levelGeom_.push_back(mkInst(wx, 0.f, wz, 0.f,
                                            kTile, kWallH, kTile,
                                            shade(R.wall, jitter), 0.f,
                                            DI_MESH_BOX, 0));
                levelGeom_.push_back(mkInst(wx, kWallH, wz, 0.f,
                                            kTile * 1.05f, 0.16f, kTile * 1.05f,
                                            shade(R.trim, jitter), 0.04f,
                                            DI_MESH_BOX, 0));
            }
        }
    }

    // atmospheric floor sheen for the wet/cold biomes
    if (region_ == 2 || region_ == 3 || region_ == 0) {
        for (int y = 0; y < d.height; y += 2)
            for (int x = 0; x < d.width; x += 2) {
                if (!isFloorTile(d.at(x, y))) continue;
                float wx = (float)x * kTile, wz = (float)y * kTile;
                levelGeomTrans_.push_back(mkInst(wx, 0.02f, wz, 0.f,
                                                 kTile * 2.f, 1.f, kTile * 2.f,
                                                 withAlpha(R.accent, 34), 0.55f,
                                                 DI_MESH_QUAD, DI_FLAG_UNLIT, (float)((x * 3 + y * 7) % 40) * 0.1f));
            }
    }

    // room dressing: pillars, braziers, torches
    for (const auto& r : d.rooms) {
        const float inset = 1.2f;
        const float px0 = ((float)r.x + inset) * kTile;
        const float pz0 = ((float)r.y + inset) * kTile;
        const float px1 = ((float)(r.x + r.w) - inset) * kTile;
        const float pz1 = ((float)(r.y + r.h) - inset) * kTile;

        for (const auto& c : { math::Vec3(px0, 0.f, pz0), math::Vec3(px1, 0.f, pz1) }) {
            levelGeom_.push_back(mkInst(c.x, 0.f, c.z, 0.f,
                                        0.85f, kWallH * 0.8f, 0.85f,
                                        R.trim, 0.f, DI_MESH_BOX, 0));
            levelGeom_.push_back(mkInst(c.x, kWallH * 0.8f, c.z, 0.f,
                                        1.1f, 0.2f, 1.1f,
                                        R.trim, 0.05f, DI_MESH_BOX, 0));
        }
        // brazier light (additive)
        const math::Vec3 bz((px0 + px1) * 0.5f, 0.f, pz0);
        levelGeom_.push_back(mkInst(bz.x, 0.f, bz.z, 0.f,
                                    0.7f, 0.9f, 0.7f, rgb(64, 52, 44), 0.f,
                                    DI_MESH_CYLINDER, 0));
        levelGeomAdd_.push_back(mkInst(bz.x, 1.0f, bz.z, 0.f,
                                       1.9f, 1.9f, 1.9f, withAlpha(R.accent, 170), 2.2f,
                                       DI_MESH_SPHERE, DI_FLAG_UNLIT | DI_FLAG_BILLBOARD, 0.7f));
    }
}

// ===========================================================================
// Quests
// ===========================================================================
void GameRuntime::setupQuests(int region) {
    if (questRegion_ == region) return;
    questRegion_ = region;
    quests_.clear();

    const RegionArt& R = kRegions[std::max(0, std::min(4, region))];

    QuestTrack a;
    a.id = std::string("q_kill_") + R.id;
    a.title = std::string("Cull the ") + R.name;
    a.objective = "kill";
    a.target = 14 + region * 6;
    a.rewardGold = 180 + region * 140;
    a.rewardXp = 150 + region * 190;

    QuestTrack b;
    b.id = std::string("q_boss_") + R.id;
    b.title = std::string("Slay ") + R.boss;
    b.objective = "boss";
    b.target = 1;
    b.rewardGold = 500 + region * 340;
    b.rewardXp = 520 + region * 430;
    b.bossQuest = true;

    QuestTrack c;
    c.id = std::string("q_chest_") + R.id;
    c.title = "Reliquary Hunter";
    c.objective = "chest";
    c.target = 3;
    c.rewardGold = 220 + region * 90;
    c.rewardXp = 180 + region * 120;

    quests_ = { a, b, c };
}

void GameRuntime::updateQuests(const std::string& what, int delta) {
    for (auto& q : quests_) {
        if (q.done || q.objective != what) continue;
        q.progress = std::min(q.target, q.progress + delta);
        if (q.progress < q.target) continue;
        q.done = true;
        gold_ += q.rewardGold;
        toast(DI_EVENT_QUEST, "Quest complete — %s  (+%d gold)", q.title.c_str(), q.rewardGold);
        awardXp(q.rewardXp);
        postSound(audio::Sound::QuestComplete, player_.position(), 0.85f, 1.f);
        saveDirtyTimer_ = 6.f;
    }
}

// ===========================================================================
// Progression
// ===========================================================================
void GameRuntime::awardXp(int amount) {
    if (amount <= 0) return;
    if (!levelSys_.addXp(amount, level_, xp_, skillPoints_)) return;
    toast(DI_EVENT_LEVELUP, "Level %d reached — a skill point is yours.", level_);
    postSound(audio::Sound::LevelUp, player_.position(), 1.f, 1.f);
    buildLoadout();
    player_.stats().health = player_.stats().maxHealth;
    resource_.current = resource_.maximum;
    Effect fx;
    fx.pos = player_.position() + math::Vec3(0, 1.f, 0);
    fx.dur = 1.1f; fx.size0 = 1.f; fx.size1 = 7.f;
    fx.rgba = withAlpha(rgb(255, 214, 120), 220);
    fx.mesh = DI_MESH_SPHERE; fx.flags = DI_FLAG_UNLIT; fx.additive = true;
    effects_.push_back(fx);
    saveDirtyTimer_ = 5.f;
}

float GameRuntime::playerDamageMult() const {
    float m = 1.f + (level_ - 1) * 0.035f;
    if (ultBuffTimer_ > 0.f) m *= 2.f;
    return m;
}

math::Vec3 GameRuntime::aimOrigin() const {
    return player_.position() + math::Vec3(0.f, 1.15f, 0.f) + player_.aimDir() * 0.7f;
}

// ===========================================================================
// Input
// ===========================================================================
void GameRuntime::setMove(float x, float y) {
    inputMove_.x = std::max(-1.f, std::min(1.f, x));
    inputMove_.y = std::max(-1.f, std::min(1.f, y));
    if (inputMove_.length() > 0.18f) { navPath_.clear(); hasNavGoal_ = false; }
}
void GameRuntime::setAim(float x, float y) {
    inputAim_.x = std::max(-1.f, std::min(1.f, x));
    inputAim_.y = std::max(-1.f, std::min(1.f, y));
}
void GameRuntime::setFire(bool pressed) { inputFire_ = pressed; }
void GameRuntime::setDash(bool pressed) { inputDash_ = pressed; }
void GameRuntime::setAbility(int slot, bool pressed) {
    if (slot < 0 || slot >= DI_MAX_ABILITIES) return;
    inputAbility_[slot] = pressed;
}
void GameRuntime::clickToMove(const math::Vec3& world) {
    pendingClick_ = true;
    pendingClickPos_ = world;
}
void GameRuntime::cameraPan(float deltaDeg) {
    camYawOffset_ = std::max(-45.f, std::min(45.f, camYawOffset_ + deltaDeg));
    camera_.applyUserYaw(deltaDeg);
}

// ===========================================================================
// Simulation
// ===========================================================================
void GameRuntime::tick(float dt) {
    if (!bootDone_) return;
    if (paused_) return;
    if (dt <= 0.f) return;
    if (dt > 0.1f) dt = 0.1f;

    time_ += dt;
    playSeconds_ += dt;

    // hit-stop freezes the world for a few milliseconds on big hits
    if (feedback_.update(dt) <= 0.f) return;

    for (int i = 0; i < DI_MAX_ABILITIES; ++i)
        if (abilityCd_[i] > 0.f) abilityCd_[i] = std::max(0.f, abilityCd_[i] - dt);
    loadoutMgr_.tickCooldowns(loadout_, dt);
    loadoutMgr_.tickResource(resource_, dt);
    soulAbilities_.tick(dt);

    if (ultBuffTimer_ > 0.f) ultBuffTimer_ -= dt;
    if (potionCd_ > 0.f) potionCd_ -= dt;
    if (bossContactCd_ > 0.f) bossContactCd_ -= dt;
    if (playerHurtFlash_ > 0.f) playerHurtFlash_ -= dt;
    if (shakeAmount_ > 0.f) shakeAmount_ = std::max(0.f, shakeAmount_ - dt * 1.6f);
    noHitTimer_ += dt;

    if (saveDirtyTimer_ > 0.f) {
        saveDirtyTimer_ -= dt;
        if (saveDirtyTimer_ <= 0.f) writeSave();
    }
    autosaveTimer_ += dt;
    if (autosaveTimer_ >= 45.f) { autosaveTimer_ = 0.f; writeSave(); }

    // rising-edge ability input
    for (int i = 0; i < DI_MAX_ABILITIES; ++i) {
        if (inputAbility_[i] && !prevAbility_[i]) tryAbility(i);
        prevAbility_[i] = inputAbility_[i];
    }

    updateWorldEvents(dt);
    updateAudioState(dt);

    if (dead_) {
        camera_.follow(player_.position(), dt);
        updateEffects(dt);
        return;
    }

    updateMovement(dt);
    updateCombat(dt);
    updateEnemies(dt);
    updateBoss(dt);
    updateProjectiles(dt);
    updateEffects(dt);
    updatePickups(dt);
    updateInteraction();

    // slow out-of-combat recovery
    if (noHitTimer_ > 3.f && player_.stats().health < player_.stats().maxHealth)
        player_.heal(player_.stats().maxHealth * 0.035f * dt);
    if (resource_.current < resource_.maximum && resource_.regenPerSec <= 0.f)
        resource_.current = std::min(resource_.maximum, resource_.current + resource_.maximum * 0.045f * dt);
}

void GameRuntime::updateMovement(float dt) {
    player::InputState in;
    in.move = inputMove_;
    in.aim = inputAim_;
    in.fire = inputFire_;
    in.dashPressed = inputDash_ && !prevDash_;
    prevDash_ = inputDash_;
    in.interactPressed = false;
    in.cameraYawDeg = camera_.config().yawDeg + camYawOffset_;

    // ---- click-to-move pathing -------------------------------------------
    if (pendingClick_) {
        pendingClick_ = false;
        hasNavGoal_ = true;
        navPath_.clear();
        if (nav_) navPath_ = nav_->findPath(player_.position(), pendingClickPos_);
        pathRepathTimer_ = 1.4f;
    }

    bool navActive = false;
    if (hasNavGoal_) {
        if (!navPath_.empty()) {
            math::Vec3 wp = navPath_.front();
            math::Vec3 to = wp - player_.position(); to.y = 0;
            if (to.length() < 0.75f) navPath_.erase(navPath_.begin());
        }

        pathRepathTimer_ -= dt;
        if (navPath_.empty() && pathRepathTimer_ <= 0.f) {
            pathRepathTimer_ = 1.0f;
            if (nav_) {
                auto fresh = nav_->findPath(player_.position(), pendingClickPos_);
                if (!fresh.empty()) navPath_ = std::move(fresh);
            }
        }

        // Follow the next waypoint when we have one; otherwise push straight
        // at the goal so a click is never silently ignored. A* legitimately
        // fails when the destination sits on unwalkable geometry (a boss
        // corpse pressed into a wall, for example) — input must still respond.
        const math::Vec3 aim = navPath_.empty() ? pendingClickPos_ : navPath_.front();
        math::Vec3 to = aim - player_.position();
        to.y = 0.f;
        if (to.length() > 0.6f) {
            in.clickToMove = true;
            in.hasMouseWorld = true;
            in.mouseWorld = aim;
            navActive = true;
        } else if (navPath_.empty()) {
            hasNavGoal_ = false;
        }
    }

    // ---- auto-aim when no right-stick / cursor is given --------------------
    if (!navActive && inputAim_.length() <= 0.18f) {
        const math::Vec3 pp = player_.position();
        float best = 18.f * 18.f;
        const Enemy* target = nullptr;
        for (const auto& en : enemies_) {
            if (en.corpseTimer > 0.f || en.e.stats.hp <= 0.f) continue;
            float d = (en.e.position - pp).lengthSq();
            if (d < best) { best = d; target = &en; }
        }
        if (target) { in.hasMouseWorld = true; in.mouseWorld = target->e.position; }
    }

    // ---- stats driven by level / affixes ----------------------------------
    auto& st = player_.stats();
    const float baseSpeed = classDef_ ? classDef_->movementSpeed : 4.8f;
    st.moveSpeed = baseSpeed * affixSpeed_ * (ultBuffTimer_ > 0.f ? 1.30f : 1.f);

    // ---- integrate + resolve against the tile grid -------------------------
    const math::Vec3 before = player_.position();
    player_.update(dt, in);
    // A successful dash leaves dashTimer parked exactly at dashCooldown.
    if (in.dashPressed && st.dashTimer >= st.dashCooldown - 1e-3f)
        postSound(audio::Sound::Dash, player_.position(), 0.5f, rng_.rangeF(0.94f, 1.1f));

    math::Vec3 after = player_.position();
    after.y = 0.f;
    if (!walkableWorld(after)) {
        const math::Vec3 sx{after.x, 0.f, before.z};
        const math::Vec3 sz{before.x, 0.f, after.z};
        if (walkableWorld(sx))      player_.setPosition(sx);
        else if (walkableWorld(sz)) player_.setPosition(sz);
        else                        player_.setPosition(math::Vec3(before.x, 0.f, before.z));
        pathRepathTimer_ = 0.f; // blocked: recut the path next frame
    }

    camera_.follow(player_.position(), dt);
}

bool GameRuntime::walkableWorld(const math::Vec3& p) const {
    if (!nav_) return true;
    const int tx = (int)std::floor(p.x / kTile + 0.5f);
    const int ty = (int)std::floor(p.z / kTile + 0.5f);
    return nav_->walkable(tx, ty);
}

void GameRuntime::queuePath(const math::Vec3& goal) {
    if (!nav_) return;
    navPath_ = nav_->findPath(player_.position(), goal);
    hasNavGoal_ = true;
    pathRepathTimer_ = 1.4f;
}

math::Vec3 GameRuntime::snapWalkable(const math::Vec3& p) const {
    if (walkableWorld(p) || !nav_) return p;
    const int cx = (int)std::floor(p.x / kTile + 0.5f);
    const int cy = (int)std::floor(p.z / kTile + 0.5f);
    for (int r = 1; r <= 6; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const bool edge = (dx == -r || dx == r || dy == -r || dy == r);
                if (!edge || !nav_->walkable(cx + dx, cy + dy)) continue;
                return math::Vec3((float)(cx + dx) * kTile, 0.f, (float)(cy + dy) * kTile);
            }
        }
    }
    return p;
}

// ---------------------------------------------------------------------------
void GameRuntime::updateCombat(float dt) {
    weaponSys_.update(weapon_, dt);

    if (weapon_.reloading && !prevReloading_)
        postSound(audio::Sound::Reload, aimOrigin(), 0.5f, rng_.rangeF(0.95f, 1.08f));
    prevReloading_ = weapon_.reloading;

    const bool fireEdge = inputFire_ && !prevFire_;
    prevFire_ = inputFire_;

    if (inputFire_) {
        weaponSys_.fire(weapon_, aimOrigin(), player_.aimDir(),
            [this](const math::Vec3& o, const math::Vec3& v, float dmg, const std::string& col) {
                auto* p = playerProj_.spawn();
                if (!p) return;
                p->position = o;
                p->velocity = v;
                p->damage = dmg * playerDamageMult();
                p->radius = 0.55f;
                p->lifetime = 1.7f;
                p->pierce = 0;
                p->color = col;
                p->ownerTag = "player";
            });
        if (fireEdge) {
            Effect fx;
            fx.pos = aimOrigin();
            fx.dur = 0.16f;
            fx.size0 = 0.5f;
            fx.size1 = 1.1f;
            fx.rgba = withAlpha(parseHex(weapon_.projColor, rgb(255, 210, 120)), 200);
            fx.mesh = DI_MESH_SPHERE;
            fx.flags = DI_FLAG_UNLIT;
            fx.additive = true;
            effects_.push_back(fx);

            audio::Sound report = audio::Sound::GunShot;
            switch (weapon_.kind) {
                case combat::WeaponKind::Shotgun:
                case combat::WeaponKind::Launcher: report = audio::Sound::GunShotHeavy; break;
                case combat::WeaponKind::Staff:    report = audio::Sound::CastFire; break;
                case combat::WeaponKind::Wand:     report = audio::Sound::CastFrost; break;
                default: break;
            }
            postSound(report, aimOrigin(), 0.6f, rng_.rangeF(0.94f, 1.1f));
        }
    }

    // damage-over-time on the player
    const float dot = playerStatus_.update(dt);
    if (dot > 0.f) damagePlayer(dot, player_.position());
}

bool GameRuntime::tryAbility(int slot) {
    if (slot < 0 || slot >= DI_MAX_ABILITIES || dead_) return false;
    if (abilityCd_[slot] > 0.f) {
        postSound(audio::Sound::UIError, player_.position(), 0.3f, 1.f);
        return false;
    }
    if (!loadoutMgr_.tryCast(loadout_, slot, resource_)) {
        postSound(audio::Sound::UIError, player_.position(), 0.35f, 1.08f);
        return false;
    }

    const auto& as = loadout_.active[slot];
    const auto* def = progression::SkillLibrary::instance().find(as.skillId);
    if (!def) return true;

    const float rankMult = def->ranks.empty() ? 1.f : def->ranks[std::max(0, as.rank)].damageMult;
    const float cdMult   = def->ranks.empty() ? 1.f : def->ranks[std::max(0, as.rank)].cooldownMult;

    abilityCd_[slot] = def->cooldown * cdMult;
    abilityCdMax_[slot] = def->cooldown * cdMult;

    auto hasTag = [&](const char* t) {
        return std::find(def->tags.begin(), def->tags.end(), std::string(t)) != def->tags.end();
    };

    const math::Vec3 pp = player_.position();
    const float reach = def->range > 0.f ? std::min(def->range, 9.f) : 0.f;
    math::Vec3 center = reach > 0.f ? pp + player_.aimDir() * reach : pp;
    const float radius = def->radius > 0.f ? def->radius : (reach > 0.f ? reach : 0.f);

    // --- mobility: blink/dash toward the aim direction ---------------------
    if (def->category == progression::SkillCategory::Mobility || hasTag("dash")) {
        const float leap = reach > 0.f ? reach : 5.5f;
        math::Vec3 dst = pp + player_.aimDir() * leap;
        dst.y = 0.f;
        if (walkableWorld(dst)) player_.setPosition(dst);
        player_.stats().invulnTimer = std::max(player_.stats().invulnTimer, 0.4f);
        center = player_.position();
    }

    // --- defensive: brief invulnerability + a heal -------------------------
    if (def->category == progression::SkillCategory::Defensive || hasTag("heal")) {
        player_.stats().invulnTimer =
            std::max(player_.stats().invulnTimer, def->duration > 0.f ? def->duration : 2.5f);
        if (hasTag("heal") || def->baseDamage <= 0.f)
            player_.heal(player_.stats().maxHealth * (hasTag("heal") ? 0.28f : 0.14f));
    }

    // --- ultimate: global damage/speed buff --------------------------------
    if (def->category == progression::SkillCategory::Ultimate) {
        ultBuffTimer_ = std::max(ultBuffTimer_, def->duration > 0.f ? def->duration : 8.f);
        camera_.shake(0.4f, 3.5f);
    }

    // --- damage ------------------------------------------------------------
    if (def->baseDamage > 0.f) {
        const float hitR = radius > 0.f ? radius : 3.8f;
        const bool coneOnly = (radius <= 0.f);
        for (auto& en : enemies_) {
            if (en.corpseTimer > 0.f || en.e.stats.hp <= 0.f) continue;
            const math::Vec3 delta = en.e.position - center;
            if (delta.lengthSq() > (hitR + 1.0f) * (hitR + 1.0f)) continue;
            if (coneOnly) {
                math::Vec3 to = en.e.position - pp; to.y = 0;
                if (to.length() > 1e-3f && to.normalized().dot(player_.aimDir()) < 0.15f) continue;
            }
            const bool crit = rng_.range01() < player_.stats().critChance;
            float dmg = def->baseDamage * rankMult * playerDamageMult();
            if (crit) dmg *= player_.stats().critMult;
            damageEnemy(en, dmg, crit);
            feedback_.addDamageNumber(en.e.position + math::Vec3(0.f, 1.5f, 0.f), dmg, crit,
                                      crit ? "#FBBF24" : "#FFFFFF");
        }
    }

    // --- VFX ---------------------------------------------------------------
    Effect fx;
    fx.pos = center;
    fx.dur = 0.55f;
    fx.size0 = std::max(0.6f, radius * 0.45f);
    fx.size1 = std::max(1.8f, radius > 0.f ? radius : 3.8f);
    const uint32_t tint = parseHex(classDef_ ? classDef_->resourceColorHex : "#A5854C",
                                   rgb(255, 214, 130));
    fx.rgba = withAlpha(tint, 210);
    fx.mesh = (def->category == progression::SkillCategory::Ultimate) ? DI_MESH_CYLINDER
                                                                     : DI_MESH_SPHERE;
    fx.flags = DI_FLAG_UNLIT;
    fx.additive = true;
    effects_.push_back(fx);

    feedback_.shake(0.08f, def->category == progression::SkillCategory::Ultimate ? 3.5f : 1.4f);

    // audio: pick the cue from category first, then class flavour.
    audio::Sound cue;
    if (def->category == progression::SkillCategory::Mobility || hasTag("dash"))
        cue = audio::Sound::Dash;
    else if (def->category == progression::SkillCategory::Ultimate)
        cue = audio::Sound::Ultimate;
    else if (def->category == progression::SkillCategory::Defensive)
        cue = hasTag("heal") ? audio::Sound::Heal : audio::Sound::CastHoly;
    else if (def->category == progression::SkillCategory::Utility)
        cue = audio::Sound::CastShadow;
    else {
        switch (classId_) {
            case 0:  cue = audio::Sound::CastHoly;   break;
            case 1:  cue = audio::Sound::CastShadow; break;
            case 2:  cue = audio::Sound::CastFire;   break;
            case 3:  cue = audio::Sound::BowShot;    break;
            default: cue = audio::Sound::CastShock;  break;
        }
    }
    postSound(cue, pp,
              def->category == progression::SkillCategory::Ultimate ? 1.f : 0.7f,
              rng_.rangeF(0.95f, 1.07f));
    return true;
}

// ---------------------------------------------------------------------------
void GameRuntime::postSound(audio::Sound id, const math::Vec3& at,
                            float gain, float pitch) {
    const math::Vec3 campos = camera_.position();
    math::Vec3 to = at - campos;
    const float dist = to.length();
    // Inverse-ish falloff: full strength nearby, never fully silent so a
    // far-off boss roar still reads as atmosphere.
    const float fade = std::max(0.18f, std::min(1.f, 1.f - dist / 42.f));
    float pan = 0.f;
    if (dist > 0.5f) {
        // Horizontal right vector derived from where the camera looks
        // (camera -> player), then projected onto the sound direction.
        math::Vec3 fwd = player_.position() - campos;
        fwd.y = 0.f;
        if (fwd.lengthSq() > 1e-4f && to.lengthSq() > 1e-6f) {
            fwd = fwd.normalized();
            const math::Vec3 right(-fwd.z, 0.f, fwd.x);
            to.y = 0.f;
            pan = to.normalized().dot(right);
        }
    }
    audio_.post(id, gain * fade, pitch, std::max(-1.f, std::min(1.f, pan)));
}

void GameRuntime::updateAudioState(float dt) {
    // Combat detection: any live enemy closing in keeps the music hot; the
    // timer is the hysteresis so the score does not flap at the edge.
    const math::Vec3 pp = player_.position();
    bool threat = false;
    for (const auto& en : enemies_) {
        if (en.corpseTimer > 0.f || en.e.stats.hp <= 0.f) continue;
        if ((en.e.position - pp).lengthSq() < 26.f * 26.f) { threat = true; break; }
    }
    if (threat || playerHurtFlash_ > 0.f) combatTimer_ = 4.f;
    else if (combatTimer_ > 0.f) combatTimer_ = std::max(0.f, combatTimer_ - dt);
    const bool inCombat = combatTimer_ > 0.f;
    audio_.setInCombat(inCombat);
    audio_.setAmbient(ambientForRegion(region_));

    using audio::MusicMood;
    MusicMood mood = MusicMood::Explore;
    if (dead_)                 mood = MusicMood::Death;
    else if (bossIndex_ >= 0 && (size_t)bossIndex_ < enemies_.size() &&
             enemies_[(size_t)bossIndex_].e.stats.hp > 0.f &&
             enemies_[(size_t)bossIndex_].e.stats.state != combat::AIState::Dead)
                               mood = MusicMood::Boss;
    else if (spireMode_)       mood = inCombat ? MusicMood::Combat : MusicMood::Spire;
    else if (inCombat)         mood = MusicMood::Combat;
    audio_.setMusic(mood);
}

// ---------------------------------------------------------------------------
void GameRuntime::updateEnemies(float dtGame) {
    const math::Vec3 pp = player_.position();

    for (size_t i = 0; i < enemies_.size(); ++i) {
        Enemy& en = enemies_[i];
        if (en.hitFlash > 0.f) en.hitFlash = std::max(0.f, en.hitFlash - dtGame);

        if (en.e.stats.state == combat::AIState::Dead) {
            en.corpseTimer -= dtGame;
            continue;
        }
        if (en.e.stats.hp <= 0.f) {
            en.e.stats.state = combat::AIState::Dead;
            en.corpseTimer = en.boss ? 8.f : 4.5f;
            killEnemy(i);
            continue;
        }
        if (en.boss) continue; // driven by updateBoss()

        const math::Vec3 before = en.e.position;
        const bool attacked = enemyAI_.update(en.e, pp, dtGame,
            [this](const math::Vec3& o, const math::Vec3& v, float dmg) {
                auto* p = enemyProj_.spawn();
                if (!p) return;
                p->position = o + math::Vec3(0.f, 1.2f, 0.f);
                p->velocity = v;
                p->damage = dmg;
                p->radius = 0.5f;
                p->lifetime = 2.4f;
                p->color = "#f87171";
                p->ownerTag = "enemy";
            });
        if (!walkableWorld(en.e.position)) en.e.position = before;

        if (attacked && !en.e.stats.ranged) {
            math::Vec3 d = pp - en.e.position; d.y = 0;
            if (d.length() <= en.e.stats.attackRange + 1.0f)
                damagePlayer(en.e.stats.dmg, en.e.position);
        }
    }

    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
        [](const Enemy& e) {
            return e.corpseTimer <= 0.f && e.e.stats.state == combat::AIState::Dead;
        }), enemies_.end());

    // corpse removal can shift indices — re-locate the live boss
    if (bossIndex_ >= 0) {
        int found = -1;
        for (size_t i = 0; i < enemies_.size(); ++i) {
            if (enemies_[i].boss) { found = (int)i; break; }
        }
        if (found != bossIndex_) {
            bossIndex_ = found;
            if (bossIndex_ < 0) boss_.reset();
            else if (boss_) boss_->base = enemies_[(size_t)bossIndex_].e;
        }
    }
}

void GameRuntime::updateBoss(float dtGame) {
    if (bossIndex_ < 0 || !boss_) return;
    if ((size_t)bossIndex_ >= enemies_.size()) { bossIndex_ = -1; boss_.reset(); return; }

    Enemy& en = enemies_[(size_t)bossIndex_];
    if (en.e.stats.state == combat::AIState::Dead) return;

    const math::Vec3 pp = player_.position();
    const math::Vec3 prevPos = en.e.position;
    boss_->base = en.e;

    enemyAI_.update(boss_->base, pp, dtGame,
        [this](const math::Vec3& o, const math::Vec3& v, float dmg) {
            auto* p = enemyProj_.spawn();
            if (!p) return;
            p->position = o + math::Vec3(0.f, 2.2f, 0.f);
            p->velocity = v;
            p->damage = dmg;
            p->radius = 0.6f;
            p->lifetime = 2.6f;
            p->color = "#fb923c";
            p->ownerTag = "enemy";
        });
    boss_->base.position.y = 0.f;
    bossAI_.update(*boss_, pp, dtGame);
    en.e = boss_->base;
    en.e.position.y = 0.f;
    if (!walkableWorld(en.e.position))
        en.e.position = walkableWorld(prevPos) ? prevPos : snapWalkable(prevPos);

    // presentation of the phase machine
    const int ph = (int)boss_->phase;
    if (ph != bossPhaseShown_) {
        if (!bossIntroShown_ && boss_->phase == combat::BossPhase::Phase1) {
            bossIntroShown_ = true;
            toast(DI_EVENT_BOSS, "%s, %s — the fight begins.", kRegions[region_].bossTitle,
                  boss_->name.c_str());
            postSound(audio::Sound::BossRoar, en.e.position, 1.f, 0.85f);
            camera_.shake(0.7f, 3.f);
        } else if (boss_->phase == combat::BossPhase::Phase2) {
            toast(DI_EVENT_DANGER, "%s enrages!", boss_->name.c_str());
            postSound(audio::Sound::BossRoar, en.e.position, 1.f, 1.14f);
            camera_.shake(0.6f, 4.f);
            bossAtkTimer_ = 1.2f;
        }
        bossPhaseShown_ = ph;
    }
    if (boss_->phase == combat::BossPhase::Intro || boss_->phase == combat::BossPhase::Transition)
        return;

    // melee contact
    math::Vec3 to = pp - en.e.position; to.y = 0;
    const float dist = to.length();
    if (dist < 3.0f && bossContactCd_ <= 0.f) {
        damagePlayer(en.e.stats.dmg * 0.5f, en.e.position);
        bossContactCd_ = 1.3f;
    }

    // telegraphed special attacks
    bossAtkTimer_ -= dtGame;
    if (bossAtkTimer_ > 0.f) return;
    const bool enraged = (boss_->phase == combat::BossPhase::Phase2);
    const int kind = rng_.range(0, 2);

    if (kind == 0) {
        Effect fx;
        fx.pos = pp;
        fx.dur = 1.15f;
        fx.size0 = 1.4f;
        fx.size1 = 7.0f;
        fx.rgba = withAlpha(rgb(255, 84, 60), 200);
        fx.mesh = DI_MESH_CYLINDER;
        fx.flags = DI_FLAG_UNLIT;
        fx.additive = true;
        fx.triggerAt = 1.0f;
        fx.radius = 6.2f;
        fx.damage = en.e.stats.dmg * (enraged ? 2.0f : 1.5f);
        fx.hitsPlayer = true;
        effects_.push_back(fx);
        toast(DI_EVENT_BOSS, "%s slams the ground!", boss_->name.c_str());
        bossAtkTimer_ = enraged ? 3.2f : 4.6f;
    } else if (kind == 1) {
        math::Vec3 dir = pp - en.e.position; dir.y = 0;
        dir = dir.length() > 1e-3f ? dir.normalized() : math::Vec3(0, 0, 1);
        for (int k = -2; k <= 2; ++k) {
            const float a = (float)k * 0.22f;
            const float cs = std::cos(a), sn = std::sin(a);
            math::Vec3 d{ dir.x * cs - dir.z * sn, 0.f, dir.x * sn + dir.z * cs };
            auto* p = enemyProj_.spawn();
            if (!p) continue;
            p->position = en.e.position + math::Vec3(0.f, 2.2f, 0.f);
            p->velocity = d * 24.f;
            p->damage = en.e.stats.dmg * 0.7f;
            p->radius = 0.65f;
            p->lifetime = 3.0f;
            p->color = "#fb923c";
            p->ownerTag = "enemy";
        }
        bossAtkTimer_ = enraged ? 2.8f : 4.2f;
    } else {
        math::Vec3 dir = pp - en.e.position; dir.y = 0;
        if (dir.length() > 2.5f) {
            en.e.position = pp - dir.normalized() * 2.8f;
            Effect fx;
            fx.pos = en.e.position;
            fx.dur = 0.5f;
            fx.size0 = 3.f;
            fx.size1 = 7.f;
            fx.rgba = withAlpha(rgb(255, 140, 48), 200);
            fx.mesh = DI_MESH_SPHERE;
            fx.flags = DI_FLAG_UNLIT;
            fx.additive = true;
            effects_.push_back(fx);
            damagePlayer(en.e.stats.dmg * 0.85f, en.e.position);
            camera_.shake(0.35f, 4.f);
        }
        bossAtkTimer_ = enraged ? 4.0f : 6.0f;
    }
}

// ---------------------------------------------------------------------------
void GameRuntime::updateProjectiles(float dtGame) {
    playerProj_.update(dtGame);
    enemyProj_.update(dtGame);

    playerProj_.forEachAlive([this](combat::Projectile& p) {
        for (auto& en : enemies_) {
            if (en.corpseTimer > 0.f || en.e.stats.hp <= 0.f) continue;
            const math::Vec3 c = en.e.position + math::Vec3(0.f, 1.0f, 0.f);
            const float rr = 1.0f + p.radius;
            if ((p.position - c).lengthSq() > rr * rr) continue;
            const bool crit = rng_.range01() < player_.stats().critChance;
            float dmg = p.damage * (crit ? player_.stats().critMult : 1.f);
            damageEnemy(en, dmg, crit);
            feedback_.addDamageNumber(c + math::Vec3(0.f, 0.5f, 0.f), dmg, crit,
                                      crit ? "#FBBF24" : "#FFFFFF");
            if (p.pierce-- <= 0) { p.alive = false; break; }
        }
    });

    if (!dead_) {
        const math::Vec3 c = player_.position() + math::Vec3(0.f, 1.0f, 0.f);
        enemyProj_.forEachAlive([&](combat::Projectile& p) {
            if (!p.alive) return;
            if ((p.position - c).lengthSq() > 1.15f * 1.15f) return;
            p.alive = false;
            damagePlayer(p.damage, p.position);
        });
    }
}

// ---------------------------------------------------------------------------
void GameRuntime::damagePlayer(float amount, const math::Vec3& from) {
    (void)from;
    if (dead_ || amount <= 0.f) return;
    if (player_.invulnerable()) return;

    const float armor = classDef_ ? classDef_->armor : 12.f;
    const float red = armor / (armor + 70.f + 9.f * (float)level_);
    const float dmg = amount * (1.f - std::min(0.75f, red));

    player_.takeDamage(dmg);
    noHitTimer_ = 0.f;
    playerHurtFlash_ = 0.35f;
    postSound(audio::Sound::PlayerHurt, player_.position(), 0.85f, rng_.rangeF(0.95f, 1.12f));
    shakeAmount_ = std::min(1.f, shakeAmount_ + 0.55f);
    feedback_.shake(0.22f, 3.f);
    camera_.shake(0.22f, 2.4f);

    if (player_.dead() && !dead_) {
        dead_ = true;
        player_.stats().health = 0.f;
        postSound(audio::Sound::PlayerDeath, player_.position(), 1.f, 1.f);
        toast(DI_EVENT_DANGER, "You have fallen in the %s.", kRegions[region_].name);
        writeSave();
    }
}

void GameRuntime::damageEnemy(Enemy& en, float amount, bool crit) {
    if (en.corpseTimer > 0.f || en.e.stats.hp <= 0.f) return;
    en.e.stats.hp -= amount;
    en.hitFlash = 0.18f;
    postSound(crit ? audio::Sound::CritHit : audio::Sound::EnemyHit,
              en.e.position, crit ? 0.75f : 0.35f, rng_.rangeF(0.92f, 1.12f));
    if (player_.stats().lifesteal > 0.f)
        player_.heal(amount * player_.stats().lifesteal);
    if (amount > 45.f) feedback_.hitstop(0.03f);
}

void GameRuntime::killEnemy(size_t idx) {
    if (idx >= enemies_.size()) return;
    Enemy& en = enemies_[idx];

    ++kills_;
    awardXp((int)en.xp);
    updateQuests("kill", 1);
    if (!en.boss)
        postSound(audio::Sound::EnemyDie, en.e.position, 0.6f, rng_.rangeF(0.88f, 1.18f));

    const float g = rng_.rangeF(en.goldMin, en.goldMax) * affixLoot_ * (en.elite ? 3.f : 1.f);
    if (g > 1.f) {
        Pickup pk;
        pk.pos = en.e.position;
        pk.kind = 0;
        pk.gold = (int)g;
        pickups_.push_back(pk);
    }
    const int extra = en.boss ? 8 : (en.elite ? 2 : 0);
    if (extra > 0)
        dropLoot(en.e.position, extra,
                 0.4f + (en.elite ? 0.6f : 0.f) + (spireMode_ ? spireFloor_ * 0.05f : 0.f), 1.f);

    if (en.boss) killBoss();
    saveDirtyTimer_ = 20.f;
}

void GameRuntime::killBoss() {
    if (bossIndex_ < 0) return;
    const math::Vec3 pos = enemies_[(size_t)bossIndex_].e.position;
    const std::string name = boss_ ? boss_->name : std::string("The Warden");

    toast(DI_EVENT_BOSS, "%s has fallen!", name.c_str());
    postSound(audio::Sound::BossDie, pos, 1.f, 0.85f);
    updateQuests("boss", 1);

    gold_ += 140 + region_ * 90 + stage_ * 45;
    dropLoot(pos, 6, 1.6f + affixLoot_, 1.f);
    awardXp((int)(260.f + (float)(region_ * 4 + stage_) * 60.f));
    potions_ = std::min(3, potions_ + 1);

    portal_.pos = snapWalkable(pos);
    portal_.kind = 0;
    portal_.alive = true;

    if (stage_ == 3 && region_ + 1 < 5)
        unlockedRegions_ = std::max(unlockedRegions_, region_ + 2);

    if (spireMode_) {
        spireFloorBest_ = std::max(spireFloorBest_, spireRun_.floor);
        toast(DI_EVENT_INFO, "Floor %d cleared — score %d.", spireRun_.floor, spireRun_.score);
    } else if (stage_ == 3 && region_ == 4 && !campaignComplete_) {
        campaignComplete_ = true;
        postSound(audio::Sound::Victory, player_.position(), 1.f, 1.f);
        toast(DI_EVENT_QUEST,
              "The Shattered Wilds are restored. Thank you for playing the campaign.");
    }

    bossIndex_ = -1;
    boss_.reset();
    bossPhaseShown_ = -1;
    saveDirtyTimer_ = 0.1f;
}

void GameRuntime::dropLoot(const math::Vec3& pos, int count, float luck, float goldMult) {
    for (int i = 0; i < count; ++i) {
        loot::Item it = roller_.rollWeapon(level_ + region_ * 3, luck * affixLoot_);
        Pickup pk;
        const float a = rng_.rangeF(0.f, 6.28318f);
        const float r = rng_.rangeF(0.4f, 2.2f);
        pk.pos = pos + math::Vec3(std::cos(a) * r, 0.f, std::sin(a) * r);
        pk.kind = 1;
        pk.item = it;
        pk.age = 0.f;
        pickups_.push_back(pk);
    }
    if (goldMult > 0.f && rng_.chance(0.7f)) {
        Pickup pk;
        pk.pos = pos;
        pk.kind = 0;
        pk.gold = (int)(rng_.range(6, 26) * goldMult * affixLoot_ * (1.f + level_ * 0.4f));
        pickups_.push_back(pk);
    }
}

// ---------------------------------------------------------------------------
void GameRuntime::updatePickups(float dt) {
    const math::Vec3 pp = player_.position();
    for (auto& pk : pickups_) {
        if (!pk.alive) continue;
        pk.age += dt;
        math::Vec3 d = pp - pk.pos;
        d.y = 0.f;
        const float dist = d.length();
        const float attract = pk.kind == 0 ? 5.5f : 1.8f;
        if (dist < attract && dist > 1e-3f)
            pk.pos += d.normalized() * (pk.kind == 0 ? 16.f : 6.5f) * dt;

        if (dist < 1.2f) {
            if (pk.kind == 0) {
                gold_ += pk.gold;
                updateQuests("gold", pk.gold);
                postSound(audio::Sound::GoldPickup, pk.pos, 0.45f,
                          rng_.rangeF(0.95f, 1.16f));
                pk.alive = false;
            } else if ((int)inventory_.size() >= DI_MAX_INVENTORY) {
                prompt_ = "Inventory full";
                postSound(audio::Sound::UIError, player_.position(), 0.35f, 1.f);
            } else {
                inventory_.push_back(pk.item);
                toast(DI_EVENT_LOOT, "%s  (%s)", pk.item.name.c_str(),
                      loot::rarityName(pk.item.rarity));
                const bool sting = pk.item.rarity >= loot::Rarity::Legendary;
                postSound(sting ? audio::Sound::LegendaryDrop : audio::Sound::ItemPickup,
                          pk.pos, sting ? 0.95f : 0.55f, 1.f);
                pk.alive = false;
                saveDirtyTimer_ = 10.f;
            }
        }
        if (pk.age > 150.f) pk.alive = false;
    }
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(),
                                  [](const Pickup& p) { return !p.alive; }),
                   pickups_.end());
}

void GameRuntime::updateEffects(float dt) {
    const math::Vec3 pp = player_.position();

    for (size_t i = 0; i < effects_.size(); ++i) {
        effects_[i].age += dt;
        const float age = effects_[i].age;
        const bool fireNow = (effects_[i].triggerAt > 0.f && !effects_[i].fired &&
                              age >= effects_[i].triggerAt);
        if (!fireNow) continue;

        effects_[i].fired = true;
        const math::Vec3 pos = effects_[i].pos;
        const float radius = effects_[i].radius;
        const float damage = effects_[i].damage;
        const bool hitsPlayer = effects_[i].hitsPlayer;
        const uint32_t col = effects_[i].rgba;

        if (hitsPlayer) {
            math::Vec3 d = pp - pos;
            d.y = 0.f;
            if (d.length() <= radius) damagePlayer(damage, pos);
        }

        Effect burst;
        burst.pos = pos;
        burst.dur = 0.45f;
        burst.size0 = radius > 0.f ? radius : 1.5f;
        burst.size1 = (radius > 0.f ? radius : 1.5f) * 1.3f;
        burst.rgba = col;
        burst.mesh = DI_MESH_SPHERE;
        burst.flags = DI_FLAG_UNLIT;
        burst.additive = true;
        effects_.push_back(burst);
        camera_.shake(0.18f, 2.5f);
    }

    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                  [](const Effect& e) { return e.age >= e.dur; }),
                   effects_.end());
}

// ---------------------------------------------------------------------------
void GameRuntime::updateInteraction() {
    prompt_.clear();
    const math::Vec3 pp = player_.position();

    for (auto& c : chestProps_) {
        if (!c.alive || c.chest.opened) continue;
        if ((c.pos - pp).length() > 3.4f) continue;
        prompt_ = c.chest.isMimic() ? "Open the Rusted Reliquary" : "Open Chest";
        return;
    }
    if (portal_.alive && (portal_.pos - pp).length() < 3.6f) {
        prompt_ = spireMode_ ? "Ascend to the Next Floor" : "Enter the Next Depth";
        return;
    }
    if (spireExit_.alive && (spireExit_.pos - pp).length() < 3.6f)
        prompt_ = "Leave the Infinity Spire";
}

void GameRuntime::interact() {
    if (!bootDone_ || dead_) return;
    const math::Vec3 pp = player_.position();

    for (auto& c : chestProps_) {
        if (!c.alive || c.chest.opened) continue;
        if ((c.pos - pp).length() > 3.6f) continue;

        c.chest.playerLevel = level_;
        loot::ChestDrop drop;
        const bool ok = chestSys_.open(c.chest, drop, roller_);
        if (!ok) { // mimic
            Enemy en;
            en.e.typeId = "mimic";
            en.e.position = c.pos;
            en.e.stats.maxHp = 160.f + (float)level_ * 28.f;
            en.e.stats.hp = en.e.stats.maxHp;
            en.e.stats.dmg = 14.f + (float)level_ * 3.f;
            en.e.stats.speed = 4.2f;
            en.e.stats.aggroRange = 40.f;
            en.e.stats.attackRange = 2.4f;
            en.e.stats.fireRate = 1.1f;
            en.e.stats.state = combat::AIState::Idle;
            en.e.fireTimer = 1.f;
            en.elite = true;
            en.xp = 70.f + (float)level_ * 18.f;
            en.goldMin = 30.f;
            en.goldMax = 80.f;
            enemies_.push_back(en);
            toast(DI_EVENT_DANGER, "The reliquary had teeth — a Mimic!");
            postSound(audio::Sound::MimicBite, c.pos, 1.f, rng_.rangeF(0.9f, 1.05f));
            camera_.shake(0.4f, 3.f);
            return;
        }
        postSound(audio::Sound::ChestOpen, c.pos, 0.8f, rng_.rangeF(0.96f, 1.06f));
        gold_ += drop.gold;
        for (auto& it : drop.items) {
            if ((int)inventory_.size() >= DI_MAX_INVENTORY) break;
            inventory_.push_back(it);
            toast(DI_EVENT_LOOT, "%s  (%s)", it.name.c_str(), loot::rarityName(it.rarity));
        }
        updateQuests("chest", 1);
        toast(DI_EVENT_INFO, "You found %d gold.", drop.gold);
        saveDirtyTimer_ = 8.f;
        return;
    }

    if (portal_.alive && (portal_.pos - pp).length() < 4.0f) {
        postSound(audio::Sound::Portal, portal_.pos, 0.9f, 1.f);
        if (spireMode_) {
            spire_.advance(spireRun_);
            spireFloor_ = spireRun_.floor;
            region_ = std::max(0, std::min(4, region_));
            stage_ = spireFloor_;
            generateLevel(region_, spireFloor_, true);
            toast(DI_EVENT_DANGER, "Infinity Spire — Floor %d", spireFloor_);
            postSound(audio::Sound::SpireFloor, player_.position(), 0.9f, 1.05f);
        } else {
            if (stage_ < 3) {
                ++stage_;
            } else if (region_ < 4) {
                region_ = region_ + 1;
                stage_ = 0;
                unlockedRegions_ = std::max(unlockedRegions_, region_ + 1);
                setupQuests(region_);
                toast(DI_EVENT_INFO, "You enter the %s.", kRegions[region_].name);
                postSound(audio::Sound::QuestAccept, player_.position(), 0.6f, 1.f);
            } else {
                campaignComplete_ = true;
                toast(DI_EVENT_QUEST, "There is nowhere left to descend. The Wilds are whole.");
                return;
            }
            generateLevel(region_, stage_, false);
            potions_ = 3;
        }
        saveDirtyTimer_ = 0.2f;
        return;
    }

    if (spireExit_.alive && (spireExit_.pos - pp).length() < 4.0f) {
        postSound(audio::Sound::Portal, spireExit_.pos, 0.8f, 1.f);
        exitSpire();
    }
}

// ---------------------------------------------------------------------------
void GameRuntime::updateWorldEvents(float dt) {
    eventTimer_ -= dt;
    if (bloodMoonTimer_ > 0.f) bloodMoonTimer_ -= dt;

    if (eventTimer_ > 0.f || dead_) return;
    eventTimer_ = 150.f + rng_.rangeF(0.f, 70.f);    const int pick = rng_.range(0, 2);

    if (pick == 0) {
        bloodMoonTimer_ = 30.f;
        toast(DI_EVENT_DANGER, "Blood Moon rising — the fallen grow bold.");
        postSound(audio::Sound::Thunder, player_.position(), 0.95f, rng_.rangeF(0.85f, 1.f));
        const RegionArt& R = kRegions[region_];
        const math::Vec3 pp = player_.position();
        for (int i = 0; i < 6; ++i) {
            const float a = rng_.rangeF(0.f, 6.28318f);
            const float r = rng_.rangeF(7.f, 12.f);
            math::Vec3 p = pp + math::Vec3(std::cos(a) * r, 0.f, std::sin(a) * r);
            if (!walkableWorld(p)) continue;
            Enemy en;
            en.e.typeId = "blood_elite";
            en.e.position = p;
            en.e.stats.maxHp = 120.f + (float)level_ * 24.f;
            en.e.stats.hp = en.e.stats.maxHp;
            en.e.stats.dmg = 12.f + (float)level_ * 2.4f;
            en.e.stats.speed = 4.4f;
            en.e.stats.aggroRange = 40.f;
            en.e.stats.attackRange = 2.3f;
            en.e.stats.fireRate = 1.1f;
            en.e.stats.state = combat::AIState::Idle;
            en.e.fireTimer = 1.f;
            en.elite = true;
            en.xp = 55.f + (float)level_ * 14.f;
            en.goldMin = 18.f;
            en.goldMax = 52.f;
            enemies_.push_back(en);
        }
        (void)R;
    } else if (pick == 1) {
        toast(DI_EVENT_INFO, "A treasure gleams nearby — the earth yields its tithe.");
        postSound(audio::Sound::WorldEvent, player_.position(), 0.6f, 1.1f);
        dropLoot(player_.position() + math::Vec3(rng_.rangeF(-4.f, 4.f), 0.f,
                                                 rng_.rangeF(-4.f, 4.f)),
                 2, 1.2f, 2.f);
    } else {
        toast(DI_EVENT_INFO, "Sanctuary's blessing: health and resource restored.");
        postSound(audio::Sound::Shrine, player_.position(), 0.75f, 1.f);
        player_.heal(player_.stats().maxHealth);
        resource_.current = resource_.maximum;
    }
}

// ===========================================================================
// Game flow
// ===========================================================================
void GameRuntime::respawn() {
    if (!dead_) return;
    dead_ = false;
    player_.stats().health = player_.stats().maxHealth;
    player_.stats().invulnTimer = 2.5f;
    resource_.current = resource_.maximum;
    player_.setPosition(spawnWorld_);
    player_.clearNavTarget();
    navPath_.clear();
    hasNavGoal_ = false;
    const int lost = gold_ / 10;
    gold_ -= lost;
    potions_ = std::max(1, potions_);
    enemyProj_.clear();
    camera_ = player::GameCamera();
    camera_.follow(spawnWorld_, 1.f);
    toast(lost > 0 ? DI_EVENT_INFO : DI_EVENT_DANGER,
          "You rise again. %d gold was lost to the dark.", lost);
    postSound(audio::Sound::Shrine, spawnWorld_, 0.85f, 0.92f);
    writeSave();
}

void GameRuntime::travel(int region) {
    if (!bootDone_) return;
    if (region < 0 || region > 4) return;
    if (region != region_ && region >= unlockedRegions_) {
        toast(DI_EVENT_DANGER, "That region is still sealed.");
        return;
    }
    if (spireMode_) {
        toast(DI_EVENT_DANGER, "Leave the Spire before travelling.");
        return;
    }
    postSound(audio::Sound::Portal, player_.position(), 0.85f, 1.f);
    region_ = region;
    stage_ = 0;
    setupQuests(region_);
    postSound(audio::Sound::QuestAccept, player_.position(), 0.6f, 1.f);
    generateLevel(region_, stage_, false);
    dead_ = false;
    player_.stats().health = player_.stats().maxHealth;
    resource_.current = resource_.maximum;
    toast(DI_EVENT_INFO, "You arrive in the %s.", kRegions[region_].name);
    saveDirtyTimer_ = 0.2f;
}

void GameRuntime::startSpire() {
    if (!bootDone_ || spireMode_) return;
    prevRegion_ = region_;
    prevStage_ = stage_;
    spireMode_ = true;
    spireRun_ = spire_.startRun(1);
    spireFloor_ = spireRun_.floor;
    // Map the run's biome string through kRegions ids — not a hand-copied
    // list, whose order disagreed with kRegions (frozen_spire and
    // sunken_crypts were swapped).
    for (int i = 0; i < 5; ++i)
        if (spireRun_.biome == kRegions[i].id) { region_ = i; break; }
    stage_ = spireFloor_;
    generateLevel(region_, spireFloor_, true);
    dead_ = false;
    player_.stats().health = player_.stats().maxHealth;
    resource_.current = resource_.maximum;
    toast(DI_EVENT_DANGER, "The Infinity Spire — Floor %d", spireFloor_);
    postSound(audio::Sound::SpireFloor, player_.position(), 0.95f, 1.f);
    saveDirtyTimer_ = 0.2f;
}

void GameRuntime::exitSpire() {
    if (!spireMode_) return;
    spireMode_ = false;
    region_ = prevRegion_;
    stage_ = prevStage_;
    generateLevel(region_, stage_, false);
    dead_ = false;
    player_.stats().health = player_.stats().maxHealth;
    resource_.current = resource_.maximum;
    toast(DI_EVENT_INFO, "You step back into the %s.", kRegions[region_].name);
    postSound(audio::Sound::Portal, player_.position(), 0.75f, 1.f);
    saveDirtyTimer_ = 0.2f;
}

void GameRuntime::equipItem(int index) {
    if (index < 0 || index >= (int)inventory_.size()) return;
    equippedIdx_ = index;
    buildLoadout();
    toast(DI_EVENT_LOOT, "Equipped %s", inventory_[(size_t)index].name.c_str());
    postSound(audio::Sound::UIClick, player_.position(), 0.45f, 1.f);
    saveDirtyTimer_ = 4.f;
}

void GameRuntime::usePotion() {
    if (!bootDone_ || dead_ || potions_ <= 0 || potionCd_ > 0.f) return;
    --potions_;
    potionCd_ = 6.f;
    player_.heal(player_.stats().maxHealth * 0.45f);
    player_.stats().invulnTimer = std::max(player_.stats().invulnTimer, 0.6f);
    Effect fx;
    fx.pos = player_.position() + math::Vec3(0.f, 1.f, 0.f);
    fx.dur = 0.6f; fx.size0 = 0.8f; fx.size1 = 3.4f;
    fx.rgba = withAlpha(rgb(220, 38, 38), 210);
    fx.mesh = DI_MESH_SPHERE; fx.flags = DI_FLAG_UNLIT; fx.additive = true;
    effects_.push_back(fx);
    postSound(audio::Sound::PotionDrink, player_.position(), 0.7f,
              rng_.rangeF(0.97f, 1.06f));
}

void GameRuntime::saveNow() { writeSave(); }

void GameRuntime::devUnlockAllRegions() { unlockedRegions_ = 5; }

void GameRuntime::devSetBossHealth(float fraction01) {
    if (bossIndex_ < 0 || (size_t)bossIndex_ >= enemies_.size()) return;
    Enemy& en = enemies_[(size_t)bossIndex_];
    if (en.e.stats.state == combat::AIState::Dead) return;
    en.e.position = snapWalkable(en.e.position);
    en.e.stats.hp = en.e.stats.maxHp * std::max(0.f, std::min(1.f, fraction01));
}

// ===========================================================================
// Snapshots
// ===========================================================================
void GameRuntime::rebuildInstances() {
    instOpaque_.clear();
    instTrans_.clear();
    instAdd_.clear();

    const RegionArt& R = kRegions[std::max(0, std::min(4, region_))];

    // ---- corpses ----------------------------------------------------------
    for (const auto& en : enemies_) {
        if (en.e.stats.state != combat::AIState::Dead) continue;
        const float t = std::max(0.f, std::min(1.f, en.corpseTimer / 1.5f));
        const uint32_t col = withAlpha(shade(en.boss ? R.accent : rgb(70, 60, 56), 0.55f),
                                       (uint8_t)(255.f * t));
        instOpaque_.push_back(mkInst(en.e.position.x, 0.f, en.e.position.z, 0.f,
                                     1.1f, 0.22f, 1.1f, col, 0.f, DI_MESH_CAPSULE, 0));
    }

    // ---- living enemies ---------------------------------------------------
    for (const auto& en : enemies_) {
        if (en.e.stats.state == combat::AIState::Dead) continue;
        const bool boss = en.boss;
        math::Vec3 dir = en.e.targetDir; dir.y = 0;
        const float yaw = (dir.length() > 1e-3f) ? std::atan2(dir.x, dir.z) : 0.f;
        const int skin = kArchSkin[std::max(0, std::min(3, en.arch))];

        uint32_t col = boss ? R.accent : shade(R.enemyColor[skin], en.elite ? 1.18f : 1.f);
        if (en.hitFlash > 0.f) col = shade(col, 1.f + en.hitFlash * 3.5f);

        const float s = boss ? 1.f : kArch[std::max(0, std::min(3, en.arch))].size;
        const float bodyH = boss ? 3.4f : (1.10f * s * (en.elite ? 1.3f : 1.f));
        const float bodyW = boss ? 2.1f : (0.85f * s * (en.elite ? 1.25f : 1.f));

        instOpaque_.push_back(mkInst(en.e.position.x, 0.f, en.e.position.z, yaw,
                                     bodyW, bodyH, bodyW, col,
                                     boss ? 0.35f : 0.05f, DI_MESH_CAPSULE, 0,
                                     time_ * 3.f));
        // head
        instOpaque_.push_back(mkInst(en.e.position.x, bodyH * 0.92f, en.e.position.z, yaw,
                                     bodyW * 0.72f, bodyW * 0.72f, bodyW * 0.72f,
                                     shade(col, 1.12f), boss ? 0.5f : 0.08f,
                                     DI_MESH_SPHERE, 0, time_ * 3.f));
        if (boss) {
            for (int k = -1; k <= 1; k += 2) {
                instOpaque_.push_back(
                    mkInst(en.e.position.x + std::cos(yaw) * 0.9f * (float)k, bodyH,
                           en.e.position.z - std::sin(yaw) * 0.9f * (float)k, yaw,
                           0.5f, 1.3f, 0.5f, shade(R.accent, 1.25f), 0.6f,
                           DI_MESH_CONE, 0, time_ * 3.f));
            }
            instAdd_.push_back(mkInst(en.e.position.x, 0.05f, en.e.position.z, 0.f,
                                      6.5f, 1.f, 6.5f, withAlpha(R.accent, 90), 1.4f,
                                      DI_MESH_QUAD, DI_FLAG_UNLIT, time_ * 2.f));
        }
        if (en.elite) {
            instAdd_.push_back(mkInst(en.e.position.x, 0.04f, en.e.position.z, 0.f,
                                      2.4f, 1.f, 2.4f,
                                      withAlpha(rgb(250, 200, 80), 140), 1.6f,
                                      DI_MESH_QUAD, DI_FLAG_UNLIT, time_ * 3.f));
        }
        // world-space health pips (billboarded quads)
        const float frac = std::max(0.f, std::min(1.f, en.e.stats.hp / en.e.stats.maxHp));
        if (frac < 0.999f && !boss) {
            instTrans_.push_back(mkInst(en.e.position.x, bodyH + 0.35f, en.e.position.z, 0.f,
                                        1.6f, 1.f, 0.16f, rgb(16, 16, 20), 0.f,
                                        DI_MESH_QUAD, DI_FLAG_BILLBOARD, 0.f));
            instTrans_.push_back(mkInst(en.e.position.x - (1.f - frac) * 0.78f,
                                        bodyH + 0.35f, en.e.position.z, 0.f,
                                        1.56f * frac, 1.f, 0.18f, rgb(226, 60, 60), 0.7f,
                                        DI_MESH_QUAD, DI_FLAG_BILLBOARD, 0.f));
        }
    }

    // ---- player -----------------------------------------------------------
    if (!dead_) {
        const math::Vec3 pp = player_.position();
        math::Vec3 aim = player_.aimDir(); aim.y = 0;
        const float yaw = (aim.length() > 1e-3f) ? std::atan2(aim.x, aim.z) : 0.f;
        const uint32_t cls = kClassColor[std::max(0, std::min(4, classId_))];

        instAdd_.push_back(mkInst(pp.x, 0.04f, pp.z, time_ * 1.5f,
                                  2.6f, 1.f, 2.6f,
                                  withAlpha(cls, ultBuffTimer_ > 0.f ? 170 : 90), 1.5f,
                                  DI_MESH_QUAD, DI_FLAG_UNLIT, time_ * 2.f));
        instOpaque_.push_back(mkInst(pp.x, 0.f, pp.z, yaw,
                                     0.9f, 1.15f, 0.9f, cls, 0.12f,
                                     DI_MESH_CAPSULE, 0, time_ * 2.f));
        instOpaque_.push_back(mkInst(pp.x, 1.14f, pp.z, yaw,
                                     0.44f, 0.44f, 0.44f, rgb(232, 208, 184), 0.06f,
                                     DI_MESH_SPHERE, 0, time_ * 2.f));
        // weapon slab pointed along the aim vector
        const math::Vec3 wp = pp + aim * 0.75f;
        instOpaque_.push_back(mkInst(wp.x, 0.95f, wp.z, yaw,
                                     0.14f, 0.14f, 1.15f, rgb(74, 66, 58), 0.10f,
                                     DI_MESH_BOX, 0, time_ * 2.f));
        if (ultBuffTimer_ > 0.f) {
            instAdd_.push_back(mkInst(pp.x, 0.f, pp.z, 0.f,
                                      1.6f, 3.0f, 1.6f, withAlpha(rgb(255, 214, 120), 120), 1.8f,
                                      DI_MESH_CYLINDER, DI_FLAG_UNLIT, time_ * 4.f));
        }
    }

    // ---- pickups ----------------------------------------------------------
    for (const auto& pk : pickups_) {
        if (!pk.alive) continue;
        if (pk.kind == 0) {
            instAdd_.push_back(mkInst(pk.pos.x, 0.45f + std::sin(time_ * 4.f + pk.age) * 0.08f,
                                      pk.pos.z, time_ * 3.f,
                                      0.42f, 0.42f, 0.42f, rgb(255, 206, 84), 1.7f,
                                      DI_MESH_SPHERE, DI_FLAG_UNLIT, time_ * 4.f));
        } else {
            const int r = std::max(0, std::min(5, (int)pk.item.rarity));
            const uint32_t c = kRarityColor[r];
            instAdd_.push_back(mkInst(pk.pos.x, 0.02f, pk.pos.z, 0.f,
                                      2.2f, 1.f, 2.2f, withAlpha(c, 150), 1.6f,
                                      DI_MESH_QUAD, DI_FLAG_UNLIT, time_ * 2.f));
            instAdd_.push_back(mkInst(pk.pos.x, 0.f, pk.pos.z, 0.f,
                                      0.5f, 5.5f, 0.5f, withAlpha(c, 110), 1.9f,
                                      DI_MESH_CYLINDER, DI_FLAG_UNLIT, time_ * 2.f));
            instOpaque_.push_back(mkInst(pk.pos.x, 0.35f, pk.pos.z, time_ * 1.6f,
                                         0.55f, 0.55f, 0.55f, shade(c, 1.1f), 0.6f,
                                         DI_MESH_BOX, 0, time_ * 3.f));
        }
    }

    // ---- chests -----------------------------------------------------------
    for (const auto& c : chestProps_) {
        if (!c.alive) continue;
        const bool mimic = c.chest.isMimic();
        const uint32_t body = mimic ? rgb(112, 40, 44)
                          : (c.chest.opened ? rgb(74, 54, 36) : rgb(104, 70, 38));
        instOpaque_.push_back(mkInst(c.pos.x, 0.f, c.pos.z, 0.f,
                                     1.5f, 0.95f, 1.05f, body, 0.04f, DI_MESH_BOX, 0));
        instOpaque_.push_back(mkInst(c.pos.x, 0.78f, c.pos.z, 0.f,
                                     1.56f, 0.18f, 1.1f,
                                     c.chest.opened ? rgb(120, 100, 70) : rgb(212, 176, 96),
                                     0.12f, DI_MESH_BOX, 0));
        if (!c.chest.opened) {
            instAdd_.push_back(mkInst(c.pos.x, 0.05f, c.pos.z, time_,
                                      2.4f, 1.f, 2.4f,
                                      withAlpha(mimic ? rgb(226, 60, 60) : rgb(250, 200, 90), 130),
                                      1.5f, DI_MESH_QUAD, DI_FLAG_UNLIT, time_ * 2.f));
        }
    }

    // ---- portals ----------------------------------------------------------
    auto pushPortal = [&](const Portal& pt) {
        if (!pt.alive) return;
        const uint32_t c = (pt.kind == 1) ? rgb(147, 51, 234) : R.accent;
        instAdd_.push_back(mkInst(pt.pos.x, 0.03f, pt.pos.z, time_ * 1.2f,
                                  5.0f, 1.f, 5.0f, withAlpha(c, 150), 1.8f,
                                  DI_MESH_QUAD, DI_FLAG_UNLIT, time_ * 2.f));
        instAdd_.push_back(mkInst(pt.pos.x, 0.f, pt.pos.z, time_ * 0.8f,
                                  3.4f, 3.2f, 3.4f, withAlpha(c, 120), 1.7f,
                                  DI_MESH_CYLINDER, DI_FLAG_UNLIT, time_ * 3.f));
        instAdd_.push_back(mkInst(pt.pos.x, 1.6f, pt.pos.z, 0.f,
                                  1.4f, 1.4f, 1.4f, withAlpha(c, 180), 2.2f,
                                  DI_MESH_SPHERE, DI_FLAG_UNLIT | DI_FLAG_BILLBOARD,
                                  time_ * 3.f));
    };
    pushPortal(portal_);
    pushPortal(spireExit_);

    // ---- projectiles ------------------------------------------------------
    auto pushProj = [this](combat::ProjectilePool& pool, uint32_t fallback) {
        pool.forEachAlive([&](combat::Projectile& p) {
            const uint32_t c = parseHex(p.color, fallback);
            instAdd_.push_back(mkInst(p.position.x, p.position.y, p.position.z, 0.f,
                                      0.55f, 0.55f, 0.55f, c, 2.4f,
                                      DI_MESH_SPHERE, DI_FLAG_UNLIT, time_ * 6.f));
            const math::Vec3 tail = p.position - p.velocity * 0.02f;
            instAdd_.push_back(mkInst(tail.x, tail.y, tail.z, 0.f,
                                      0.3f, 0.3f, 0.3f, withAlpha(c, 110), 1.8f,
                                      DI_MESH_SPHERE, DI_FLAG_UNLIT, time_ * 6.f));
        });
    };
    pushProj(playerProj_, rgb(255, 214, 100));
    pushProj(enemyProj_, rgb(255, 120, 96));

    // ---- effects ----------------------------------------------------------
    for (const auto& fx : effects_) {
        const float t = std::max(0.f, std::min(1.f, fx.age / std::max(0.01f, fx.dur)));
        const float s = fx.size0 + (fx.size1 - fx.size0) * t;
        uint32_t col;
        if (fx.triggerAt > 0.f && !fx.fired) {
            // telegraph: pulsing ground warning until it detonates
            const float pulse = 0.55f + 0.45f * std::sin(fx.age * 22.f);
            col = withAlpha(fx.rgba, (uint8_t)(110.f + 130.f * pulse));
        } else {
            col = withAlpha(fx.rgba, (uint8_t)(255.f * (1.f - t)));
        }
        const uint32_t flags = fx.flags | (fx.mesh == DI_MESH_QUAD ? DI_FLAG_BILLBOARD : 0u);
        instAdd_.push_back(mkInst(fx.pos.x, fx.triggerAt > 0.f && !fx.fired ? 0.06f : fx.pos.y,
                                  fx.pos.z, fx.age * 2.f, s, s, s, col,
                                  fx.additive ? 2.0f : 0.5f, fx.mesh, flags, fx.age));
    }
}

const DIInstance* GameRuntime::instanceData(int32_t& opaque, int32_t& translucent,
                                             int32_t& additive) {
    rebuildInstances();
    instAll_.clear();
    instAll_.reserve(levelGeom_.size() + instOpaque_.size() +
                     levelGeomTrans_.size() + instTrans_.size() +
                     levelGeomAdd_.size() + instAdd_.size());

    instAll_.insert(instAll_.end(), levelGeom_.begin(), levelGeom_.end());
    instAll_.insert(instAll_.end(), instOpaque_.begin(), instOpaque_.end());
    opaque = (int32_t)instAll_.size();

    instAll_.insert(instAll_.end(), levelGeomTrans_.begin(), levelGeomTrans_.end());
    instAll_.insert(instAll_.end(), instTrans_.begin(), instTrans_.end());
    translucent = (int32_t)instAll_.size() - opaque;

    instAll_.insert(instAll_.end(), levelGeomAdd_.begin(), levelGeomAdd_.end());
    instAll_.insert(instAll_.end(), instAdd_.begin(), instAdd_.end());
    additive = (int32_t)instAll_.size() - opaque - translucent;

    return instAll_.empty() ? nullptr : instAll_.data();
}

bool GameRuntime::fillCamera(DICamera& out) const {
    std::memset(&out, 0, sizeof(out));
    const math::Mat4 v = camera_.viewMatrix();
    const math::Mat4 p = camera_.projMatrix(levelAspect_);
    std::memcpy(out.view, v.m, sizeof(out.view));
    std::memcpy(out.proj, p.m, sizeof(out.proj));

    const math::Vec3 cp = camera_.position();
    out.pos[0] = cp.x; out.pos[1] = cp.y; out.pos[2] = cp.z;
    out.time = time_;

    const RegionArt& R = kRegions[std::max(0, std::min(4, region_))];
    math::Vec3 sun{ 0.42f, 0.82f, 0.38f };
    sun = sun.normalized();
    out.sunDir[0] = sun.x; out.sunDir[1] = sun.y; out.sunDir[2] = sun.z;
    out.sunIntensity = R.sunIntensity * (spireMode_ ? 0.9f : 1.f);
    unpackRgb(R.sunColor, out.sunColor);
    out.ambient = R.ambient;
    unpackRgb(R.fog, out.fogColor);
    out.fogDensity = R.fogDensity;
    out.exposure = 1.0f + playerHurtFlash_ * 0.8f;
    const float fb = feedback_.shakeTime > 0.f
                   ? feedback_.shakeIntensity * feedback_.shakeTime * 0.10f : 0.f;
    out.shake = std::min(1.f, fb + shakeAmount_);
    return true;
}

bool GameRuntime::screenToWorld(float ndcX, float ndcY, math::Vec3& out) const {
    return camera_.groundFromScreen(ndcX, ndcY, levelAspect_, out);
}

bool GameRuntime::fillHud(DIHud& out) const {
    std::memset(&out, 0, sizeof(out));
    const auto& st = player_.stats();

    out.health = st.health;
    out.healthMax = st.maxHealth;
    out.resource = resource_.current;
    out.resourceMax = resource_.maximum;
    out.xpFrac = (float)xp_ /
                 (float)std::max(1, progression::LevelSystem::xpForLevel(level_));
    out.level = level_;
    out.gold = gold_;
    out.skillPoints = skillPoints_;
    out.region = region_;
    out.unlockedRegions = unlockedRegions_;
    out.kills = kills_;
    out.spireFloor = spireMode_ ? spireFloor_ : 0;
    out.dead = dead_ ? 1 : 0;
    out.potions = potions_;
    out.abilityCount = DI_MAX_ABILITIES;

    copyStr(out.className, sizeof(out.className), classDef_ ? classDef_->displayName : "?");
    copyStr(out.resourceName, sizeof(out.resourceName), classDef_ ? classDef_->resourceName : "");
    copyStr(out.regionName, sizeof(out.regionName),
            spireMode_ ? std::string("Infinity Spire — Floor ") + std::to_string(spireFloor_)
                       : std::string(kRegions[region_].name));

    const QuestTrack* q = nullptr;
    for (const auto& t : quests_) if (!t.done) { q = &t; break; }
    if (q) {
        copyStr(out.questTitle, sizeof(out.questTitle), q->title);
        char buf[112];
        const char* verb = q->objective == "kill"   ? "Slain"
                         : q->objective == "boss"   ? "Boss defeated"
                         : q->objective == "chest"  ? "Reliquaries opened"
                                                    : "Collected";
        std::snprintf(buf, sizeof(buf), "%s: %d / %d", verb, q->progress, q->target);
        copyStr(out.questObjective, sizeof(out.questObjective), buf);
    }

    if (bossIndex_ >= 0 && boss_ && (size_t)bossIndex_ < enemies_.size()) {
        const Enemy& b = enemies_[(size_t)bossIndex_];
        if (b.e.stats.state != combat::AIState::Dead && b.e.stats.hp > 0.f) {
            out.bossActive = 1;
            out.bossFrac = std::max(0.f, std::min(1.f, b.e.stats.hp / b.e.stats.maxHp));
            copyStr(out.bossName, sizeof(out.bossName),
                    std::string(boss_->name) + ", " + kRegions[region_].bossTitle);
            out.bossX = b.e.position.x;
            out.bossZ = b.e.position.z;
        }
    }

    copyStr(out.prompt, sizeof(out.prompt), prompt_);

    const math::Vec3 pp = player_.position();
    out.playerX = pp.x;
    out.playerZ = pp.z;

    for (int i = 0; i < DI_MAX_ABILITIES; ++i) {
        DIHudAbility& a = out.abilities[i];
        const auto& as = loadout_.active[(size_t)i];
        const auto* def = as.skillId.empty()
                        ? nullptr
                        : progression::SkillLibrary::instance().find(as.skillId);
        copyStr(a.name, sizeof(a.name), def ? def->name : "-");
        a.cooldown = std::max(0.f, abilityCd_[i]);
        a.cooldownMax = abilityCdMax_[i] > 0.f ? abilityCdMax_[i] : def ? def->cooldown : 0.f;
        a.resourceCost = def ? def->resourceCost : 0.f;
        bool ready = !dead_ && abilityCd_[i] <= 0.f;
        if (ready && def &&
            def->category != progression::SkillCategory::Basic &&
            resource_.current < def->resourceCost)
            ready = false;
        a.ready = ready ? 1u : 0u;
    }
    return true;
}

int GameRuntime::fillDamageNumbers(DIDamageNumber* out, int maxCount) const {
    if (!out || maxCount <= 0) return 0;
    const auto& nums = feedback_.activeNumbers();
    int n = 0;
    for (const auto& d : nums) {
        if (n >= maxCount) break;
        DIDamageNumber& o = out[n++];
        o.x = d.position.x;
        o.y = d.position.y;
        o.z = d.position.z;
        o.value = d.value;
        o.age = d.age;
        o.maxAge = d.maxAge;
        o.rgba = parseHex(d.colorHex, rgb(255, 255, 255));
        o.crit = d.crit ? 1u : 0u;
    }
    return n;
}

bool GameRuntime::popEvent(DIEvent& out) {
    if (events_.empty()) return false;
    out = events_.front();
    events_.pop_front();
    return true;
}

int GameRuntime::inventoryCount() const { return (int)inventory_.size(); }

bool GameRuntime::inventoryItem(int index, DIItem& out) const {
    if (index < 0 || index >= (int)inventory_.size()) return false;
    const loot::Item& it = inventory_[(size_t)index];
    std::memset(&out, 0, sizeof(out));
    copyStr(out.name, sizeof(out.name), it.name);
    out.rarity = (int32_t)it.rarity;
    out.ilvl = it.ilvl;
    out.damage = it.damage;
    out.equipped = (index == equippedIdx_) ? 1 : 0;
    out.sockets = (int32_t)it.socketCount;

    std::string detail;
    for (const auto& a : it.affixes) {
        if (!detail.empty()) detail += ", ";
        detail += a.name;
    }
    if (it.socketCount > 0) {
        if (!detail.empty()) detail += " · ";
        detail += std::to_string(it.socketCount) + " socket" + (it.socketCount == 1 ? "" : "s");
    }
    copyStr(out.detail, sizeof(out.detail), detail);
    return true;
}

void GameRuntime::toast(int type, const char* fmt, ...) {
    DIEvent e{};
    e.type = type;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(e.text, sizeof(e.text), fmt, ap);
    va_end(ap);
    events_.push_back(e);
    while (events_.size() > 12) events_.pop_front();
}

// ===========================================================================
// Persistence
// ===========================================================================
std::string GameRuntime::savePath() const {
    std::string p = saveDir_;
    if (!p.empty() && p.back() != '/') p += '/';
    return p + "dionite_save.json";
}

bool GameRuntime::loadSave() {
    std::ifstream in(savePath(), std::ios::binary);
    if (!in) return false;
    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }
    if (!j.is_object() || j.value("version", -1) != kVersion) return false;

    try {
        classId_       = std::max(0, std::min(4, j.value("classId", 0)));
        campaignSeed_  = j.value("seed", (uint64_t)campaignSeed_);
        region_        = std::max(0, std::min(4, j.value("region", 0)));
        stage_         = std::max(0, std::min(3, j.value("stage", 0)));
        unlockedRegions_ = std::max(1, std::min(5, j.value("unlocked", 1)));
        level_         = std::max(1, std::min(100, j.value("level", 1)));
        xp_            = std::max(0, j.value("xp", 0));
        gold_          = std::max(0, j.value("gold", 0));
        skillPoints_   = std::max(0, j.value("skillPoints", 0));
        kills_         = std::max(0, j.value("kills", 0));
        potions_       = std::max(0, j.value("potions", 3));
        spireFloorBest_= std::max(0, j.value("spireBest", 0));
        playSeconds_   = (float)j.value("playSeconds", 0.0);
        equippedIdx_   = j.value("equipped", -1);
        campaignComplete_ = j.value("campaignComplete", false);

        // Player mixer settings travel with the save (AudioManager survives
        // boot's reset(), which only clears queue/score state).
        audio_.setMaster((float)j.value("audioMaster", 0.9));
        audio_.setMusicVolume((float)j.value("audioMusic", 0.7));
        audio_.setSfxVolume((float)j.value("audioSfx", 1.0));
        audio_.setMuted(j.value("audioMuted", false));

        inventory_.clear();
        if (j.contains("inventory") && j["inventory"].is_array()) {
            for (const auto& e : j["inventory"]) {
                loot::Item it;
                it.baseId = e.value("b", "pistol");
                it.kind = "weapon";
                it.rarity = (loot::Rarity)std::max(0, std::min(5, e.value("r", 0)));
                it.ilvl = std::max(1, e.value("i", 1));
                it.damage = (float)e.value("d", 10.0);
                it.name = e.value("n", std::string("Item"));
                it.socketCount = loot::socketsFor(it.rarity);
                it.sockets.assign((size_t)it.socketCount, "");
                inventory_.push_back(it);
            }
        }
        if (equippedIdx_ >= (int)inventory_.size()) equippedIdx_ = -1;

        questRegion_ = -1;
        setupQuests(region_);
        if (j.contains("quests") && j["quests"].is_object()) {
            for (auto& q : quests_) {
                auto it = j["quests"].find(q.id);
                if (it == j["quests"].end()) continue;
                q.progress = std::max(0, it.value().value("p", 0));
                q.done = it.value().value("d", false);
                if (q.done) q.progress = q.target;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string GameRuntime::buildSaveJson(bool pretty) const {
    json j;
    j["version"] = kVersion;
    j["classId"] = classId_;
    j["seed"] = campaignSeed_;
    j["region"] = spireMode_ ? prevRegion_ : region_;
    j["stage"] = spireMode_ ? std::max(0, std::min(3, prevStage_)) : stage_;
    j["unlocked"] = unlockedRegions_;
    j["level"] = level_;
    j["xp"] = xp_;
    j["gold"] = gold_;
    j["skillPoints"] = skillPoints_;
    j["kills"] = kills_;
    j["potions"] = potions_;
    j["spireBest"] = spireFloorBest_;
    j["playSeconds"] = playSeconds_;
    j["equipped"] = equippedIdx_;
    j["campaignComplete"] = campaignComplete_;
    j["audioMaster"] = audio_.master();
    j["audioMusic"] = audio_.musicVolume();
    j["audioSfx"] = audio_.sfxVolume();
    j["audioMuted"] = audio_.muted();

    json inv = json::array();
    for (const auto& it : inventory_)
        inv.push_back({ { "b", it.baseId }, { "r", (int)it.rarity }, { "i", it.ilvl },
                        { "d", it.damage }, { "n", it.name } });
    j["inventory"] = inv;

    json qs = json::object();
    for (const auto& q : quests_)
        qs[q.id] = { { "p", q.progress }, { "d", q.done } };
    j["quests"] = qs;

    return pretty ? j.dump(2) : j.dump();
}

void GameRuntime::writeSave() {
    if (saveDir_.empty()) return;
    std::ofstream out(savePath(), std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << buildSaveJson(true);
}

std::string GameRuntime::saveJson() const {
    return buildSaveJson(false);
}

} // namespace dionite::game
