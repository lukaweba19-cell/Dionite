// ============================================================================
// Dionite — C++ ↔ Swift bridge implementation.
//
// Every symbol declared in DioniteAPI.h. The Swift side reaches these through
// the Objective-C bridging header, so the signatures here are the contract —
// keep them and DioniteAPI.h in lock-step.
// ============================================================================
#include "DioniteAPI.h"

#include "Game/GameRuntime.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

using dionite::game::GameRuntime;

namespace {

std::unique_ptr<GameRuntime> g_rt;
float g_viewProj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
float g_vectors[12] = {0};
float g_factors[6] = {0};
float g_basis[6] = {1, 0, 0, 0, 1, 0};
DIHud g_hud{};
bool g_hudValid = false;

void writeText(char* out, int32_t cap, const char* src) {
    if (!out || cap <= 0) return;
    if (!src) src = "";
    std::snprintf(out, (size_t)cap, "%s", src);
}

} // namespace

extern "C" {

// -- Lifecycle --------------------------------------------------------------
void dionite_boot(const char* saveDir, int32_t classId, uint64_t seed, int32_t hasSave) {
    g_rt = std::make_unique<GameRuntime>();
    g_rt->boot(saveDir ? saveDir : "", (int)classId, (uint64_t)seed, hasSave != 0);
}

void dionite_shutdown(void) {
    if (g_rt) {
        g_rt->shutdown();
        g_rt.reset();
    }
}

void dionite_tick(float dt) {
    g_hudValid = false;
    if (g_rt) g_rt->tick(dt);
}

void dionite_resize(int32_t width, int32_t height) {
    if (g_rt) g_rt->resize((int)width, (int)height);
}

void dionite_pause(void)  { if (g_rt) g_rt->setPaused(true); }
void dionite_resume(void) { if (g_rt) g_rt->setPaused(false); }
int32_t dionite_version(void) { return (int32_t)GameRuntime::kVersion; }

// -- Input ------------------------------------------------------------------
void dionite_set_move(float x, float y) { if (g_rt) g_rt->setMove(x, y); }
void dionite_set_aim(float x, float y)  { if (g_rt) g_rt->setAim(x, y); }
void dionite_set_fire(int32_t pressed)  { if (g_rt) g_rt->setFire(pressed != 0); }
void dionite_set_dash(int32_t pressed)  { if (g_rt) g_rt->setDash(pressed != 0); }
void dionite_set_ability(int32_t slot, int32_t pressed) {
    if (g_rt) g_rt->setAbility((int)slot, pressed != 0);
}
void dionite_click_to_move(float x, float y, float z) {
    if (g_rt) g_rt->clickToMove(dionite::math::Vec3(x, y, z));
}
void dionite_camera_pan(float deltaDeg) { if (g_rt) g_rt->cameraPan(deltaDeg); }
void dionite_interact(void)             { if (g_rt) g_rt->interact(); }
void dionite_use_potion(void)           { if (g_rt) g_rt->usePotion(); }

// -- Game flow --------------------------------------------------------------
void dionite_respawn(void)            { if (g_rt) g_rt->respawn(); }
void dionite_travel(int32_t region)   { if (g_rt) g_rt->travel((int)region); }
void dionite_start_spire(void)        { if (g_rt) g_rt->startSpire(); }
void dionite_exit_spire(void)         { if (g_rt) g_rt->exitSpire(); }
void dionite_equip_item(int32_t idx)  { if (g_rt) g_rt->equipItem((int)idx); }
void dionite_save_now(void)           { if (g_rt) g_rt->saveNow(); }

// -- Frame snapshots --------------------------------------------------------
int32_t dionite_camera(DICamera* out) {
    if (!g_rt || !out) return 0;
    if (!g_rt->fillCamera(*out)) return 0;

    // Cache the combined matrix + lighting vectors for the Swift accessors.
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float acc = 0.f;
            for (int k = 0; k < 4; ++k) acc += out->proj[k * 4 + r] * out->view[c * 4 + k];
            g_viewProj[c * 4 + r] = acc;
        }
    }
    for (int i = 0; i < 3; ++i) {
        g_vectors[i]      = out->sunDir[i];
        g_vectors[3 + i]  = out->sunColor[i];
        g_vectors[6 + i]  = out->fogColor[i];
    }
    g_factors[0] = out->time;
    g_factors[1] = out->sunIntensity;
    g_factors[2] = out->ambient;
    g_factors[3] = out->fogDensity;
    g_factors[4] = out->exposure;
    g_factors[5] = out->shake;

    // Row 0 / row 1 of the column-major view matrix are the camera right and
    // up axes — billboards need them to face the viewer.
    g_basis[0] = out->view[0]; g_basis[1] = out->view[4]; g_basis[2] = out->view[8];
    g_basis[3] = out->view[1]; g_basis[4] = out->view[5]; g_basis[5] = out->view[9];
    return 1;
}

const float* dionite_camera_view_proj(void) { return g_viewProj; }
void dionite_camera_vectors(float* out12) {
    if (out12) std::memcpy(out12, g_vectors, sizeof(g_vectors));
}
void dionite_camera_factors(float* out6) {
    if (out6) std::memcpy(out6, g_factors, sizeof(g_factors));
}
void dionite_camera_basis(float* outRight3, float* outUp3) {
    if (outRight3) std::memcpy(outRight3, g_basis, 3 * sizeof(float));
    if (outUp3) std::memcpy(outUp3, g_basis + 3, 3 * sizeof(float));
}
int32_t dionite_screen_to_world(float ndcX, float ndcY, float* outWorld3) {
    if (!g_rt || !outWorld3) return 0;
    dionite::math::Vec3 world;
    if (!g_rt->screenToWorld(ndcX, ndcY, world)) return 0;
    outWorld3[0] = world.x;
    outWorld3[1] = world.y;
    outWorld3[2] = world.z;
    return 1;
}

int32_t dionite_hud(DIHud* out) {
    if (!g_rt || !out) return 0;
    if (!g_rt->fillHud(g_hud)) return 0;
    g_hudValid = true;
    *out = g_hud;
    return 1;
}

// The HUD string/slot accessors reuse the frame's snapshot so the Swift side
// can pull a dozen fields without re-running fillHud() each time.
static bool refreshHudCache() {
    if (g_hudValid && g_rt) return true;
    if (!g_rt || !g_rt->fillHud(g_hud)) {
        g_hudValid = false;
        return false;
    }
    g_hudValid = true;
    return true;
}

const DIInstance* dionite_instances(int32_t* opaqueCount,
                                    int32_t* translucentCount,
                                    int32_t* additiveCount) {
    static int32_t sZero = 0;
    if (!opaqueCount) opaqueCount = &sZero;
    if (!translucentCount) translucentCount = &sZero;
    if (!additiveCount) additiveCount = &sZero;
    if (!g_rt) {
        *opaqueCount = *translucentCount = *additiveCount = 0;
        return nullptr;
    }
    return g_rt->instanceData(*opaqueCount, *translucentCount, *additiveCount);
}

int32_t dionite_damage_numbers(DIDamageNumber* out, int32_t maxCount) {
    if (!g_rt) return 0;
    return g_rt->fillDamageNumbers(out, (int)maxCount);
}

int32_t dionite_pop_event(DIEvent* out) {
    if (!g_rt || !out) return 0;
    return g_rt->popEvent(*out) ? 1 : 0;
}

int32_t dionite_inventory_count(void) {
    return g_rt ? g_rt->inventoryCount() : 0;
}

int32_t dionite_inventory_item(int32_t index, DIItem* out) {
    if (!g_rt || !out) return 0;
    return g_rt->inventoryItem((int)index, *out) ? 1 : 0;
}

// -- Swift-friendly accessors -----------------------------------------------
void dionite_hud_text(int32_t field, char* out, int32_t cap) {
    if (!refreshHudCache()) { writeText(out, cap, ""); return; }
    const DIHud& h = g_hud;
    switch (field) {
        case DI_HUD_TEXT_CLASS:           writeText(out, cap, h.className); break;
        case DI_HUD_TEXT_RESOURCE:        writeText(out, cap, h.resourceName); break;
        case DI_HUD_TEXT_REGION:          writeText(out, cap, h.regionName); break;
        case DI_HUD_TEXT_QUEST_TITLE:     writeText(out, cap, h.questTitle); break;
        case DI_HUD_TEXT_QUEST_OBJECTIVE: writeText(out, cap, h.questObjective); break;
        case DI_HUD_TEXT_BOSS:            writeText(out, cap, h.bossName); break;
        case DI_HUD_TEXT_PROMPT:          writeText(out, cap, h.prompt); break;
        default:                          writeText(out, cap, ""); break;
    }
}

void dionite_ability(int32_t slot, char* outName, int32_t nameCap,
                     float* outCooldown, float* outCooldownMax,
                     uint32_t* outReady, float* outCost) {
    writeText(outName, nameCap, "");
    if (outCooldown) *outCooldown = 0.f;
    if (outCooldownMax) *outCooldownMax = 0.f;
    if (outReady) *outReady = 0;
    if (outCost) *outCost = 0.f;
    if (!refreshHudCache()) return;
    if (slot < 0 || slot >= DI_MAX_ABILITIES) return;
    const DIHudAbility& a = g_hud.abilities[slot];
    writeText(outName, nameCap, a.name);
    if (outCooldown) *outCooldown = a.cooldown;
    if (outCooldownMax) *outCooldownMax = a.cooldownMax;
    if (outReady) *outReady = a.ready;
    if (outCost) *outCost = a.resourceCost;
}

int32_t dionite_pop_event_text(int32_t* outType, char* out, int32_t cap) {
    DIEvent e{};
    if (!g_rt || !g_rt->popEvent(e)) {
        if (outType) *outType = DI_EVENT_NONE;
        writeText(out, cap, "");
        return 0;
    }
    if (outType) *outType = e.type;
    writeText(out, cap, e.text);
    return 1;
}

int32_t dionite_inventory_row(int32_t index,
                              char* outName, int32_t nameCap,
                              char* outDetail, int32_t detailCap,
                              int32_t* outRarity, int32_t* outIlvl,
                              float* outDamage, int32_t* outEquipped,
                              int32_t* outSockets) {
    writeText(outName, nameCap, "");
    writeText(outDetail, detailCap, "");
    if (outRarity) *outRarity = 0;
    if (outIlvl) *outIlvl = 0;
    if (outDamage) *outDamage = 0.f;
    if (outEquipped) *outEquipped = 0;
    if (outSockets) *outSockets = 0;
    if (!g_rt) return 0;

    DIItem it{};
    if (!g_rt->inventoryItem((int)index, it)) return 0;
    writeText(outName, nameCap, it.name);
    writeText(outDetail, detailCap, it.detail);
    if (outRarity) *outRarity = it.rarity;
    if (outIlvl) *outIlvl = it.ilvl;
    if (outDamage) *outDamage = it.damage;
    if (outEquipped) *outEquipped = it.equipped;
    if (outSockets) *outSockets = it.sockets;
    return 1;
}

int32_t dionite_skill_points(void) { return g_rt ? g_rt->skillPoints() : 0; }
int32_t dionite_spire_best(void) { return g_rt ? g_rt->spireFloorBest() : 0; }
float dionite_play_seconds(void) { return g_rt ? g_rt->playSeconds() : 0.f; }
int32_t dionite_campaign_complete(void) {
    return (g_rt && g_rt->campaignComplete()) ? 1 : 0;
}

int32_t dionite_has_save(void) {
    return (g_rt && g_rt->loadedFromSave()) ? 1 : 0;
}

int32_t dionite_save_exists(const char* saveDir) {
    if (!saveDir || !*saveDir) return 0;
    std::string p(saveDir);
    if (p.back() != '/') p += '/';
    p += "dionite_save.json";
    std::ifstream in(p, std::ios::binary);
    return in.good() ? 1 : 0;
}

void dionite_dev_unlock_all(void) {
    if (g_rt) g_rt->devUnlockAllRegions();
}
void dionite_dev_set_boss_health(float fraction01) {
    if (g_rt) g_rt->devSetBossHealth(fraction01);
}

// -- Audio ------------------------------------------------------------------
int32_t dionite_audio_poll(int32_t* outId, float* outGain,
                           float* outPitch, float* outPan) {
    if (!g_rt) return 0;
    dionite::audio::AudioEvent e;
    if (!g_rt->audio().poll(e)) return 0;
    if (outId)    *outId    = (int32_t)e.id;
    if (outGain)  *outGain  = e.gain;
    if (outPitch) *outPitch = e.pitch;
    if (outPan)   *outPan   = e.pan;
    return 1;
}

int32_t dionite_audio_pending(void) {
    return g_rt ? (int32_t)g_rt->audio().pending() : 0;
}

int32_t dionite_audio_music(void) {
    return g_rt ? (int32_t)g_rt->audio().music() : 0;
}
int32_t dionite_audio_ambient(void) {
    return g_rt ? (int32_t)g_rt->audio().ambient() : 0;
}
int32_t dionite_audio_in_combat(void) {
    return (g_rt && g_rt->audio().inCombat()) ? 1 : 0;
}

float dionite_audio_sfx_gain(void) {
    return g_rt ? g_rt->audio().sfxGain() : 0.f;
}
float dionite_audio_music_gain(void) {
    return g_rt ? g_rt->audio().musicGain() : 0.f;
}

void dionite_audio_set_master(float v)       { if (g_rt) g_rt->audio().setMaster(v); }
void dionite_audio_set_music_volume(float v) { if (g_rt) g_rt->audio().setMusicVolume(v); }
void dionite_audio_set_sfx_volume(float v)   { if (g_rt) g_rt->audio().setSfxVolume(v); }
void dionite_audio_set_muted(int32_t muted)  { if (g_rt) g_rt->audio().setMuted(muted != 0); }

float dionite_audio_master(void)       { return g_rt ? g_rt->audio().master() : 0.9f; }
float dionite_audio_music_volume(void) { return g_rt ? g_rt->audio().musicVolume() : 0.7f; }
float dionite_audio_sfx_volume(void)   { return g_rt ? g_rt->audio().sfxVolume() : 1.f; }
int32_t dionite_audio_is_muted(void) {
    return (g_rt && g_rt->audio().muted()) ? 1 : 0;
}

} // extern "C"
