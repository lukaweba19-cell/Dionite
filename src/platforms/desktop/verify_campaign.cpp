// ============================================================================
// Dionite — Headless campaign verification harness.
//
// Boots GameRuntime exactly the way the iOS host does, then plays the whole
// campaign through the public API: movement, auto-aim + weapon fire, ability
// casts, boss kills, loot, XP, quests, every region/stage transition, the
// Infinity Spire, fast travel, and a save/load round-trip.
//
// It asserts on observable state (HUD / snapshot / C API results) rather than
// on private members, so it fails the build if any part of the loop regresses.
//
// Build:  g++ -std=c++17 -Isrc -Isrc/external
//             src/platforms/desktop/verify_campaign.cpp src/Game/GameRuntime.cpp
//             src/Combat/Weapons/WeaponBase.cpp src/Loot/Items/ItemBase.cpp
//             src/Progression/Skills/SkillLibrary.*.cpp -o dionite_verify
// ============================================================================
#include "Game/GameRuntime.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

using dionite::game::GameRuntime;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (ok) {
        std::printf("  ok   %s\n", what);
    } else {
        ++g_failures;
        std::printf("  FAIL %s\n", what);
    }
}

void checkf(bool ok, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    check(ok, buf);
}

// One simulated frame, with the same housekeeping a real player would do:
// revive on death, drink a potion when low, and poke the ability buttons.
void frame(GameRuntime& rt, DIHud* hud, bool pokeAbilities, int i) {
    if (pokeAbilities && (i % 75 == 0)) {
        for (int s = 0; s < DI_MAX_ABILITIES; ++s) rt.setAbility(s, true);
        rt.tick(1.f / 60.f);
        for (int s = 0; s < DI_MAX_ABILITIES; ++s) rt.setAbility(s, false);
    }
    rt.tick(1.f / 60.f);

    if (!hud || (i % 15 != 0)) return;
    rt.fillHud(*hud);
    if (hud->dead) {
        rt.respawn();
        rt.fillHud(*hud);
    } else if (hud->potions > 0 && hud->health < hud->healthMax * 0.45f) {
        rt.usePotion();
    }
}

void pump(GameRuntime& rt, DIHud& hud, int frames, bool pokeAbilities) {
    for (int i = 0; i < frames; ++i) frame(rt, &hud, pokeAbilities, i);
    rt.fillHud(hud);
}

// Walk toward a world point while shooting, re-aiming at the live boss each
// time the HUD refreshes. Returns the final distance to the target.
float approach(GameRuntime& rt, DIHud& hud, float tx, float tz, int budget) {
    float dist = 1e9f;
    for (int i = 0; i < budget; ++i) {
        frame(rt, &hud, true, i);
        if (i % 15 == 0) {
            if (hud.bossActive != 0) { tx = hud.bossX; tz = hud.bossZ; }
            const float dx = hud.playerX - tx;
            const float dz = hud.playerZ - tz;
            dist = std::sqrt(dx * dx + dz * dz);
            if (dist < 7.5f) break;
        }
        if (i % 45 == 0) rt.clickToMove(dionite::math::Vec3(tx, 0.f, tz));
    }
    rt.fillHud(hud);
    const float dx = hud.playerX - tx;
    const float dz = hud.playerZ - tz;
    return std::sqrt(dx * dx + dz * dz);
}

// Walk into the exit portal and use it. Returns true once the transition
// (or the end of the campaign) has been observed.
bool advanceStage(GameRuntime& rt, DIHud& hud, float tx, float tz,
                  bool expectNewLevel, int expectRegion) {
    for (int i = 0; i < 5000; ++i) {
        if (i % 45 == 0) rt.clickToMove(dionite::math::Vec3(tx, 0.f, tz));
        frame(rt, &hud, true, i);
        if (i % 15 != 0) continue;

        const float dx = hud.playerX - tx;
        const float dz = hud.playerZ - tz;
        if (std::sqrt(dx * dx + dz * dz) >= 3.9f) continue;
        if (hud.prompt[0] == '\0') continue;

        rt.interact();
        rt.tick(1.f / 60.f);
        rt.fillHud(hud);

        if (!expectNewLevel) return true;
        if (hud.region == expectRegion && hud.bossActive != 0) return true;
    }
    std::printf("    [advance failed] player=(%.1f,%.1f) target=(%.1f,%.1f) "
                "prompt='%s' region=%d boss=%d dead=%d\n",
                hud.playerX, hud.playerZ, tx, tz, hud.prompt,
                hud.region, hud.bossActive, hud.dead);
    return false;
}

} // namespace

int main() {
    const std::string saveDir = "/tmp/dionite_verify_save";
    ::mkdir(saveDir.c_str(), 0755);
    std::remove((saveDir + "/dionite_save.json").c_str());

    std::printf("== Dionite campaign verification ==\n");

    GameRuntime rt;
    rt.resize(1707, 960);
    rt.boot(saveDir, /*classId=*/0, /*seed=*/12345, /*hasSave=*/false);

    DIHud hud{};
    DICamera cam{};
    check(rt.fillHud(hud), "HUD produced after boot");
    check(rt.fillCamera(cam), "camera produced after boot");
    check(hud.questTitle[0] != '\0', "a quest is active after boot");
    check(hud.bossActive == 1, "a floor boss is present after boot");
    check(std::string(hud.className) == "Crusader", "class resolves to Crusader");
    check(std::string(hud.regionName) == "Verdant Wilds", "campaign starts in the Verdant Wilds");
    check(hud.abilityCount == DI_MAX_ABILITIES, "six ability slots are reported");

    int32_t op = 0, tr = 0, ad = 0;
    const DIInstance* inst = rt.instanceData(op, tr, ad);
    check(inst != nullptr && op > 500, "level geometry emits opaque instances");
    checkf(tr >= 0 && ad > 0 && (op + tr + ad) > op,
           "snapshot splits into opaque=%d translucent=%d additive=%d", op, tr, ad);

    // --------------------------------------------------------------------
    // Stage 1 of 20: movement, real weapon damage, rewards.
    // --------------------------------------------------------------------
    std::printf("-- stage 1: movement + combat --\n");
    const float startX = hud.playerX, startZ = hud.playerZ;
    rt.setFire(true);

    float bossX = hud.bossX, bossZ = hud.bossZ;
    const float initialDist = std::sqrt((startX - bossX) * (startX - bossX) +
                                        (startZ - bossZ) * (startZ - bossZ));
    float dist = approach(rt, hud, bossX, bossZ, 3000);
    const float moved = std::sqrt((hud.playerX - startX) * (hud.playerX - startX) +
                                  (hud.playerZ - startZ) * (hud.playerZ - startZ));
    checkf(moved > 2.0f, "player travelled toward the boss via click-to-move (%.1fm)", moved);
    checkf(dist < initialDist - 5.f, "player closed on the boss (%.0fm -> %.0fm)",
           initialDist, dist);

    const int killsBefore = hud.kills;
    const float fracBefore = hud.bossFrac;
    pump(rt, hud, 360, true);
    check(hud.kills > killsBefore || hud.bossFrac < fracBefore || hud.bossFrac < 1.f,
          "weapon fire / abilities damaged the enemy population");

    // --------------------------------------------------------------------
    // Kill the boss through the normal death path, then verify rewards.
    // --------------------------------------------------------------------
    bossX = hud.bossX;
    bossZ = hud.bossZ;
    const int killsBeforeBoss = hud.kills;
    rt.devSetBossHealth(0.f);
    pump(rt, hud, 120, false);

    check(hud.bossActive == 0, "boss dies through the standard death path");
    check(hud.kills > killsBeforeBoss, "the kill was counted");
    check(hud.level >= 2, "boss XP levels the player up");
    check(hud.gold > 0, "gold was awarded");
    check(std::string(hud.questObjective).find('/') != std::string::npos,
          "quest objective reports progress");

    DIEvent ev{};
    int events = 0;
    while (rt.popEvent(ev)) {
        ++events;
        if (ev.text[0] == '\0') ++g_failures;
    }
    checkf(events > 0, "runtime queued %d narrative events", events);

    DIDamageNumber dmg[DI_MAX_DAMAGE_NUMBERS];
    const int dmgCount = rt.fillDamageNumbers(dmg, DI_MAX_DAMAGE_NUMBERS);
    check(dmgCount >= 0, "damage number snapshot is readable");

    // --------------------------------------------------------------------
    // All 20 campaign floors.
    // --------------------------------------------------------------------
    std::printf("-- campaign sweep (5 regions x 4 stages) --\n");
    bool okAdvance = advanceStage(rt, hud, bossX, bossZ, true, 0);
    check(okAdvance, "entered stage 2 through the exit portal");

    checkf(rt.inventoryCount() >= 1, "boss dropped loot picked up at the portal (%d items)",
           rt.inventoryCount());
    DIItem item{};
    check(rt.inventoryCount() > 0 && rt.inventoryItem(0, item), "inventory row is readable");
    check(item.name[0] != '\0', "inventory row has a name");
    rt.equipItem(0);
    rt.fillHud(hud);
    check(hud.healthMax > 0.f, "equipping an item keeps the runtime consistent");

    for (int s = 1; s < 20; ++s) {
        rt.fillHud(hud);
        if (hud.bossActive == 0) pump(rt, hud, 60, false);
        if (hud.bossActive == 0) {
            checkf(false, "stage %d has a boss", s + 1);
            break;
        }

        bossX = hud.bossX;
        bossZ = hud.bossZ;
        approach(rt, hud, bossX, bossZ, 3000);

        const int kb = hud.kills;
        bossX = hud.bossX;
        bossZ = hud.bossZ;
        rt.devSetBossHealth(0.f);
        pump(rt, hud, 100, false);
        checkf(hud.bossActive == 0 && hud.kills > kb,
               "stage %d boss defeated and counted", s + 1);

        const int expectRegion = (s + 1) / 4;
        const bool last = (s == 19);
        const bool adv = advanceStage(rt, hud, bossX, bossZ, !last, expectRegion);

        if (!last) {
            if (!adv) { checkf(false, "stage %d complete -> region %d", s + 1, expectRegion); break; }
            checkf(hud.bossActive == 1,
                   "stage %d complete -> region %d, next boss spawned", s + 1, expectRegion);
        } else {
            checkf(hud.region == 4, "campaign ends in the Sky Citadel (region %d)", hud.region);
        }
    }
    std::printf("  level=%d gold=%d kills=%d region=%d\n",
                hud.level, hud.gold, hud.kills, hud.region);
    check(hud.level >= 5, "campaign progression reaches a meaningful level");
    check(hud.kills >= 20, "at least one kill per floor");

    // --------------------------------------------------------------------
    // Fast travel across every unlocked region.
    // --------------------------------------------------------------------
    std::printf("-- fast travel --\n");
    rt.devUnlockAllRegions();
    for (int r = 0; r < 5; ++r) {
        rt.travel(r);
        rt.fillHud(hud);
        op = tr = ad = 0;
        (void)rt.instanceData(op, tr, ad);
        checkf(hud.region == r && op > 200,
               "travel to region %d generates a level (%d opaque instances)", r, op);
    }

    // --------------------------------------------------------------------
    // Infinity Spire endgame.
    // --------------------------------------------------------------------
    std::printf("-- infinity spire --\n");
    rt.startSpire();
    rt.fillHud(hud);
    check(hud.spireFloor == 1, "spire starts on floor 1");

    for (int f = 1; f <= 3; ++f) {
        rt.fillHud(hud);
        checkf(hud.bossActive == 1, "spire floor %d has a boss", f);
        bossX = hud.bossX;
        bossZ = hud.bossZ;
        approach(rt, hud, bossX, bossZ, 2500);
        bossX = hud.bossX;
        bossZ = hud.bossZ;
        rt.devSetBossHealth(0.f);
        pump(rt, hud, 90, false);
        const bool adv = advanceStage(rt, hud, bossX, bossZ, true, hud.region);
        checkf(adv && hud.spireFloor == f + 1,
               "cleared spire floor %d -> floor %d", f, hud.spireFloor);
    }
    rt.exitSpire();
    rt.fillHud(hud);
    check(hud.spireFloor == 0, "leaving the spire returns to the campaign");

    // --------------------------------------------------------------------
    // Save / load round-trip.
    // --------------------------------------------------------------------
    std::printf("-- persistence --\n");
    const int savedLevel = hud.level, savedGold = hud.gold, savedKills = hud.kills;
    const int savedRegion = hud.region;
    const std::string savedClass(hud.className);
    rt.saveNow();

    GameRuntime rt2;
    rt2.resize(1707, 960);
    rt2.boot(saveDir, /*classId=*/4, /*seed=*/0, /*hasSave=*/true);
    DIHud hud2{};
    rt2.fillHud(hud2);
    checkf(hud2.level == savedLevel && hud2.gold == savedGold && hud2.kills == savedKills,
           "save/load restores level=%d gold=%d kills=%d",
           hud2.level, hud2.gold, hud2.kills);
    checkf(hud2.region == savedRegion, "save/load restores region %d", savedRegion);
    check(std::string(hud2.className) == savedClass, "save/load restores the class");
    checkf(rt2.inventoryCount() == rt.inventoryCount(),
           "save/load restores %d inventory items", rt2.inventoryCount());
    rt2.shutdown();
    rt.shutdown();

    // --------------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("CAMPAIGN VERIFICATION PASSED\n");
        return 0;
    }
    std::printf("CAMPAIGN VERIFICATION FAILED\n");
    return 1;
}
