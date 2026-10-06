// ============================================================================
// Dionite — GameRuntime: the actual game loop. Owns the campaign — regions,
// procedural levels, enemies, bosses, loot, quests, progression, spire
// endgame, world events, save/load — and produces per-frame snapshots
// (instances / camera / HUD / events) consumed by the platform renderer.
//
// Mesh conventions for DIInstance (renderer must match):
//   QUAD      horizontal, centered at origin, spans ±0.5 in x/z (y=0)
//   BOX       x,z in [-0.5,0.5], y in [0,1]  (sits on the ground at pos.y)
//   CAPSULE   y in [0,1], radius 0.35        (feet at pos.y)
//   SPHERE    centered at origin, radius 0.5 (pos = center)
//   CONE      base at y=0, radius 0.5, height 1
//   CYLINDER  base at y=0, radius 0.5, height 1
// ============================================================================
#pragma once
#include "Game/Snapshot.h"

#include "Core/Math/Random.h"
#include "Player/Controller/PlayerController.h"
#include "Player/Camera/GameCamera.h"
#include "Player/Abilities/Abilities.h"
#include "World/DungeonGenerator/DungeonGenerator.h"
#include "World/DungeonGenerator/DungeonMeshBuilder.h"
#include "World/DungeonGenerator/NavGrid.h"
#include "Combat/Weapons/WeaponBase.h"
#include "Combat/Projectiles/Projectile.h"
#include "Combat/AI/EnemyAI.h"
#include "Combat/AI/BossAI.h"
#include "Combat/Effects/StatusEffect.h"
#include "Combat/Feedback/FeedbackSystem.h"
#include "Loot/Items/ItemBase.h"
#include "Loot/Chests/Chest.h"
#include "GameSystems/QuestSystem.h"
#include "GameSystems/InfinitySpire.h"
#include "Progression/Levels/LevelSystem.h"
#include "Progression/Classes/ClassRegistry.h"
#include "Progression/Skills/LoadoutManager.h"

#include <memory>
#include <string>
#include <vector>
#include <deque>

namespace dionite::game {

class GameRuntime {
public:
    GameRuntime() = default;

    // -- Lifecycle ----------------------------------------------------------
    void boot(const std::string& saveDir, int classId, uint64_t seed, bool hasSave);
    void shutdown();
    void tick(float dt);
    void resize(int width, int height);
    void setPaused(bool p) { paused_ = p; }
    bool paused() const { return paused_; }
    static constexpr int kVersion = 3;

    // -- Input (called from the platform bridge) ----------------------------
    void setMove(float x, float y);
    void setAim(float x, float y);
    void setFire(bool pressed);
    void setDash(bool pressed);
    void setAbility(int slot, bool pressed);
    void clickToMove(const math::Vec3& world);
    void cameraPan(float deltaDeg);
    void interact();
    void usePotion();
    void respawn();
    void travel(int region);
    void startSpire();
    void exitSpire();
    void equipItem(int index);
    void saveNow();

    // -- Read-only status (exposed through the platform bridge) ------------
    int   skillPoints() const { return skillPoints_; }
    int   spireFloorBest() const { return spireFloorBest_; }
    float playSeconds() const { return playSeconds_; }
    bool  campaignComplete() const { return campaignComplete_; }
    bool  loadedFromSave() const { return loadedSave_; }

    // -- Development seams --------------------------------------------------
    // Used by the headless CI verifier (and available to an in-game developer
    // console). They only *set a condition*: every reward, quest, loot roll,
    // portal and region transition still runs through the normal code path.
    void devUnlockAllRegions();
    void devSetBossHealth(float fraction01);

    // -- Snapshots ----------------------------------------------------------
    bool fillCamera(DICamera& out) const;
    /// Projects an NDC point (-1..1, y up) onto the y=0 ground plane.
    bool screenToWorld(float ndcX, float ndcY, math::Vec3& out) const;
    bool fillHud(DIHud& out) const;
    const DIInstance* instanceData(int32_t& opaque, int32_t& translucent, int32_t& additive);
    int fillDamageNumbers(DIDamageNumber* out, int maxCount) const;
    bool popEvent(DIEvent& out);
    int inventoryCount() const;
    bool inventoryItem(int index, DIItem& out) const;

private:
    // -- Content ------------------------------------------------------------
    struct Enemy {
        combat::EnemyInstance e;
        int   arch = 0;             // 0 grunt, 1 brute, 2 ranged, 3 stalker
        float xp = 10.f;
        float goldMin = 2.f, goldMax = 8.f;
        bool elite = false;
        bool boss = false;
        float corpseTimer = 0.f;   // >0 while fading out after death
        float hitFlash = 0.f;
    };
    struct Pickup {
        math::Vec3 pos;
        int kind = 0;              // 0 = gold, 1 = item
        int gold = 0;
        loot::Item item;
        float age = 0.f;
        bool alive = true;
    };
    struct ChestProp {
        math::Vec3 pos;
        loot::Chest chest;
        bool alive = true;
    };
    struct Effect {
        math::Vec3 pos;
        float age = 0.f, dur = 0.6f;
        float size0 = 1.f, size1 = 3.f;
        uint32_t rgba = 0xffffffffu;
        uint32_t mesh = DI_MESH_SPHERE;
        uint32_t flags = DI_FLAG_UNLIT;
        bool additive = true;
        // Scheduled ground hazard (telegraphed boss mechanics):
        float triggerAt = -1.f;   // fire when age >= triggerAt
        float radius = 0.f;
        float damage = 0.f;
        bool  hitsPlayer = false;
        bool  fired = false;
    };
    struct Portal {
        math::Vec3 pos;
        int kind = 0;              // 0 = exit/next stage, 1 = spire exit
        bool alive = false;
    };

    void generateLevel(int region, int stage, bool spire);
    void spawnEnemies(const world::Dungeon& d, int region, int floorNo);
    void buildLevelGeometry(const world::Dungeon& d, const std::string& biomeMat);
    void setupBoss(const world::Dungeon& d, int region, int floorNo);
    void setupQuests(int region);
    void buildLoadout();
    void rebuildInstances();

    // -- Gameplay helpers ---------------------------------------------------
    void updateMovement(float dt);
    void updateCombat(float dt);
    void updateEnemies(float dtGame);
    void updateBoss(float dtGame);
    void updateProjectiles(float dtGame);
    void updatePickups(float dt);
    void updateEffects(float dt);
    void updateInteraction();
    void updateWorldEvents(float dt);
    void updateQuests(const std::string& what, int delta = 1);
    void damagePlayer(float amount, const math::Vec3& from);
    void damageEnemy(Enemy& en, float amount, bool crit);
    void killEnemy(size_t idx);
    void killBoss();
    void dropLoot(const math::Vec3& pos, int count, float luck, float goldMult);
    void awardXp(int amount);
    void toast(int type, const char* fmt, ...);
    bool tryAbility(int slot);
    float playerDamageMult() const;
    math::Vec3 aimOrigin() const;

    bool walkableWorld(const math::Vec3& p) const;
    math::Vec3 snapWalkable(const math::Vec3& p) const;
    void queuePath(const math::Vec3& goal);

    // -- Persistence --------------------------------------------------------
    std::string savePath() const;
    bool loadSave();
    void writeSave();

    // -- Campaign / meta state ---------------------------------------------
    std::string saveDir_;
    int   classId_ = 0;
    uint64_t campaignSeed_ = 1;
    int   region_ = 0;             // current biome 0..4
    int   stage_ = 0;              // current dungeon within biome 0..3
    int   unlockedRegions_ = 1;    // biomes available for fast travel
    int   level_ = 1;
    int   xp_ = 0;
    int   gold_ = 0;
    int   skillPoints_ = 0;
    int   kills_ = 0;
    int   potions_ = 3;
    int   spireFloorBest_ = 0;
    float playSeconds_ = 0.f;
    float autosaveTimer_ = 0.f;
    bool  paused_ = false;
    bool  dead_ = false;
    bool  bootDone_ = false;
    bool  loadedSave_ = false;
    float saveDirtyTimer_ = 0.f;

    // spire / endgame
    bool spireMode_ = false;
    int  spireFloor_ = 0;
    int  prevRegion_ = 0, prevStage_ = 0;
    SpireRun spireRun_;
    InfinitySpire spire_{0x591E2EULL};

    // world events
    float eventTimer_ = 120.f;
    float bloodMoonTimer_ = 0.f;

    // -- Runtime objects ----------------------------------------------------
    player::PlayerController player_;
    player::GameCamera camera_;
    player::AbilityRegistry soulAbilities_;
    progression::PlayerLoadout loadout_{progression::ClassId::Crusader};
    progression::LoadoutManager loadoutMgr_;
    progression::ResourceState resource_;
    progression::LevelSystem levelSys_;
    const progression::ClassDefinition* classDef_ = nullptr;

    combat::WeaponBase weapon_;
    combat::WeaponSystem weaponSys_;
    combat::ProjectilePool playerProj_;
    combat::ProjectilePool enemyProj_;
    combat::EnemyAI enemyAI_;
    combat::BossAI bossAI_;
    combat::FeedbackSystem feedback_;
    combat::StatusManager playerStatus_;

    loot::LootRoller roller_{0x1234};
    loot::ChestSystem chestSys_{0xABCDEF};

    std::unique_ptr<world::DungeonGenerator> dgen_;
    world::Dungeon dungeon_;
    world::DungeonMesh mesh_;
    std::unique_ptr<world::NavGrid> nav_;

    std::vector<Enemy> enemies_;
    int bossIndex_ = -1;           // index into enemies_
    std::vector<Pickup> pickups_;
    std::vector<ChestProp> chestProps_;
    std::vector<Effect> effects_;
    Portal portal_;               // main exit/advance portal (boss pad)
    Portal spireExit_;            // spire return portal (spawn room)
    std::unique_ptr<combat::BossInstance> boss_;
    math::Random rng_{0xBEEF};

    // inventory / equipment
    std::vector<loot::Item> inventory_;
    int equippedIdx_ = -1;

    // timers / transient state
    float potionCd_ = 0.f;
    float ultBuffTimer_ = 0.f;
    float shakeAmount_ = 0.f;
    float bossContactCd_ = 0.f;
    float abilityCd_[DI_MAX_ABILITIES] = {0, 0, 0, 0, 0, 0};
    float abilityCdMax_[DI_MAX_ABILITIES] = {0.f, 1.f, 18.f, 6.f, 14.f, 90.f};
    bool  prevAbility_[DI_MAX_ABILITIES] = {false, false, false, false, false, false};
    bool  prevFire_ = false;
    bool  prevDash_ = false;
    bool  pendingClick_ = false;
    math::Vec3 pendingClickPos_;
    int   questRegion_ = -1;
    bool  bossIntroShown_ = false;
    bool  campaignComplete_ = false;
    math::Vec3 spawnWorld_;
    std::string prompt_;

    // held / edge-detected input latched by the platform bridge
    math::Vec2 inputMove_{0.f, 0.f};
    math::Vec2 inputAim_{0.f, 0.f};
    bool  inputFire_ = false;
    bool  inputDash_ = false;
    bool  inputAbility_[DI_MAX_ABILITIES] = {false, false, false, false, false, false};

    // camera pan + click-to-move navigation
    float camYawOffset_ = 0.f;
    bool  hasNavGoal_ = false;

    // boss presentation / cadence
    int   bossPhaseShown_ = -1;
    float bossAtkTimer_ = 3.f;

    // feedback timers
    float playerHurtFlash_ = 0.f;
    float noHitTimer_ = 0.f;

    // dungeon sigil affix multipliers for the current level
    float affixDmg_ = 1.f, affixHp_ = 1.f, affixLoot_ = 1.f, affixSpeed_ = 1.f;

    std::vector<math::Vec3> navPath_;
    float pathRepathTimer_ = 0.f;

    // quest state (kept simple + serializable)
    struct QuestTrack {
        std::string id, title, objective;
        int progress = 0, target = 1;
        int rewardGold = 0, rewardXp = 0;
        bool done = false;
        bool bossQuest = false;
    };
    std::vector<QuestTrack> quests_;

    // -- Snapshots ----------------------------------------------------------
    std::vector<DIInstance> instOpaque_, instTrans_, instAdd_, instAll_;
    std::deque<DIEvent> events_;
    float time_ = 0.f;
    float levelAspect_ = 16.f / 9.f;

    // level static geometry cache (split by blend pass)
    std::vector<DIInstance> levelGeom_;
    std::vector<DIInstance> levelGeomTrans_;
    std::vector<DIInstance> levelGeomAdd_;
};

} // namespace dionite::game
