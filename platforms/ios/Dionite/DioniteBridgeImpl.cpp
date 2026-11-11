// ============================================================================
// Dionite — C ABI implementation (version 2).
//
// All state below is owned by the runtime and touched only from the main
// thread. No function allocates in the per-frame path: strings are read from
// the cached DIHud, instances point into the runtime's own buffer, and damage
// numbers are copied into a fixed array.
// ============================================================================
#include "DioniteAPI.h"

#include "Game/GameRuntime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>

using dionite::game::GameRuntime;

namespace {

std::unique_ptr<GameRuntime> g_rt;
std::string g_lastError;
std::string g_saveJson;

DICamera g_camera{};
DIHud g_hud{};
bool g_hudValid = false;

DIDamageNumber g_damage[DI_MAX_DAMAGE_NUMBERS]{};
int32_t g_damageCount = 0;

const DIInstance* g_instances = nullptr;
int32_t g_opaque = 0;
int32_t g_translucent = 0;
int32_t g_additive = 0;

float g_viewProj[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

DIFrame g_frame{};

uint32_t g_textRevision = 1;

// Cached copies of the strings, used only to detect changes so the host can
// avoid rebuilding Foundation strings every frame.
char g_lastClass[24] = {0};
char g_lastResource[16] = {0};
char g_lastRegion[40] = {0};
char g_lastQuestTitle[64] = {0};
char g_lastQuestObjective[112] = {0};
char g_lastBoss[40] = {0};
char g_lastPrompt[96] = {0};
char g_lastAbility[DI_MAX_ABILITIES][24] = {{0}};

// One-frame button releases: the simulation consumes rising edges, so a press
// is held for exactly one tick and then cleared.
bool g_releaseAbility[DI_MAX_ABILITIES] = {false};
bool g_releaseDash = false;

void setError(const char* what) {
    g_lastError = what ? what : "unknown error";
}

template <typename T>
bool changed(char* cached, size_t capacity, const T* current) {
    if (std::strncmp(cached, current, capacity) == 0) return false;
    std::snprintf(cached, capacity, "%s", current);
    return true;
}

void refreshHud() {
    if (!g_rt) {
        g_hudValid = false;
        return;
    }
    g_hudValid = g_rt->fillHud(g_hud);
}

// Rebuilds the combined view-projection matrix and the derived camera vectors.
void refreshCamera() {
    float* vp = g_viewProj;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float acc = 0.f;
            for (int k = 0; k < 4; ++k) {
                acc += g_camera.proj[k * 4 + row] * g_camera.view[col * 4 + k];
            }
            vp[col * 4 + row] = acc;
        }
    }
}

void refreshTextRevision() {
    if (!g_hudValid) return;
    bool dirty = false;
    dirty |= changed(g_lastClass, sizeof(g_lastClass), g_hud.className);
    dirty |= changed(g_lastResource, sizeof(g_lastResource), g_hud.resourceName);
    dirty |= changed(g_lastRegion, sizeof(g_lastRegion), g_hud.regionName);
    dirty |= changed(g_lastQuestTitle, sizeof(g_lastQuestTitle), g_hud.questTitle);
    dirty |= changed(g_lastQuestObjective, sizeof(g_lastQuestObjective), g_hud.questObjective);
    dirty |= changed(g_lastBoss, sizeof(g_lastBoss), g_hud.bossName);
    dirty |= changed(g_lastPrompt, sizeof(g_lastPrompt), g_hud.prompt);
    for (int i = 0; i < DI_MAX_ABILITIES; ++i) {
        dirty |= changed(g_lastAbility[i], sizeof(g_lastAbility[i]), g_hud.abilities[i].name);
    }
    if (dirty) ++g_textRevision;
}

void refreshFrame() {
    if (!g_rt) {
        g_frame.camera = nullptr;
        g_frame.hud = nullptr;
        g_frame.instances = nullptr;
        g_frame.opaqueCount = 0;
        g_frame.translucentCount = 0;
        g_frame.additiveCount = 0;
        g_frame.damageNumbers = nullptr;
        g_frame.damageNumberCount = 0;
        g_frame.viewProjection = g_viewProj;
        return;
    }

    if (!g_rt->fillCamera(g_camera)) {
        std::memset(&g_camera, 0, sizeof(g_camera));
        std::memcpy(g_camera.view, g_viewProj, sizeof(g_camera.view));
    }
    refreshCamera();
    refreshHud();
    refreshTextRevision();

    g_instances = g_rt->instanceData(g_opaque, g_translucent, g_additive);
    g_damageCount = g_rt->fillDamageNumbers(g_damage, DI_MAX_DAMAGE_NUMBERS);

    g_frame.camera = &g_camera;
    g_frame.hud = g_hudValid ? &g_hud : nullptr;
    g_frame.instances = g_instances;
    g_frame.opaqueCount = g_opaque;
    g_frame.translucentCount = g_translucent;
    g_frame.additiveCount = g_additive;
    g_frame.damageNumbers = g_damageCount > 0 ? g_damage : nullptr;
    g_frame.damageNumberCount = g_damageCount;
    g_frame.viewProjection = g_viewProj;
}

void releaseOneFrameButtons() {
    if (!g_rt) return;
    for (int i = 0; i < DI_MAX_ABILITIES; ++i) {
        if (g_releaseAbility[i]) {
            g_rt->setAbility(i, false);
            g_releaseAbility[i] = false;
        }
    }
    if (g_releaseDash) {
        g_rt->setDash(false);
        g_releaseDash = false;
    }
}

} // namespace

// Compile-time ABI checks: the host indexes these arrays directly.
static_assert(sizeof(DIInstance) == 64, "DIInstance must stay 64 bytes");
static_assert(sizeof(DIHudAbility) == 40, "DIHudAbility layout changed");
static_assert(DI_MAX_ABILITIES == 6, "ability bar is six slots");
static_assert(offsetof(DIInstance, rgba) == 32, "DIInstance::rgba offset changed");
static_assert(offsetof(DIInstance, mesh) == 36, "DIInstance::mesh offset changed");
static_assert(offsetof(DIInstance, flags) == 40, "DIInstance::flags offset changed");
static_assert(sizeof(DIDamageNumber) == 32, "DIDamageNumber layout changed");
static_assert(sizeof(DICamera) == 208, "DICamera layout changed");

extern "C" {

// -- Lifecycle --------------------------------------------------------------

int32_t dionite_boot(const DIBootParams* params) noexcept {
    try {
        if (!params) {
            setError("dionite_boot: null params");
            return 0;
        }
        g_rt.reset();
        g_hud = DIHud{};
        g_hudValid = false;
        g_damageCount = 0;
        g_instances = nullptr;
        g_opaque = g_translucent = g_additive = 0;
        g_lastError.clear();

        g_rt = std::make_unique<GameRuntime>();
        g_rt->boot(params->saveDir ? params->saveDir : "", (int)params->classId,
                   (uint64_t)params->seed, params->hasSave != 0);
        if (params->width > 0 && params->height > 0) {
            g_rt->resize((int)params->width, (int)params->height);
        }
        refreshFrame();
        return 1;
    } catch (const std::exception& e) {
        setError(e.what());
        g_rt.reset();
        return 0;
    } catch (...) {
        setError("dionite_boot: unknown failure");
        g_rt.reset();
        return 0;
    }
}

void dionite_shutdown(void) noexcept {
    try {
        if (g_rt) {
            g_rt->shutdown();
            g_rt.reset();
        }
    } catch (...) {
        g_rt.reset();
    }
    g_hudValid = false;
    g_instances = nullptr;
    g_opaque = g_translucent = g_additive = 0;
    g_damageCount = 0;
    g_frame = DIFrame{};
}

int32_t dionite_is_running(void) noexcept { return g_rt ? 1 : 0; }

int32_t dionite_abi_version(void) noexcept { return DIONITE_ABI_VERSION; }

int32_t dionite_save_version(void) noexcept {
    return (int32_t)GameRuntime::kVersion;
}

const char* dionite_last_error(void) noexcept {
    return g_lastError.empty() ? "" : g_lastError.c_str();
}

void dionite_tick(float dt) noexcept {
    if (!g_rt) return;
    try {
        releaseOneFrameButtons();
        g_rt->tick(dt);
    } catch (const std::exception& e) {
        setError(e.what());
    } catch (...) {
        setError("dionite_tick: unknown failure");
    }
}

void dionite_resize(int32_t width, int32_t height) noexcept {
    if (g_rt) g_rt->resize((int)width, (int)height);
}

void dionite_set_paused(int32_t paused) noexcept {
    if (g_rt) g_rt->setPaused(paused != 0);
}

// -- Input ------------------------------------------------------------------

void dionite_move(float x, float y) noexcept { if (g_rt) g_rt->setMove(x, y); }
void dionite_aim(float x, float y) noexcept { if (g_rt) g_rt->setAim(x, y); }
void dionite_fire(int32_t held) noexcept { if (g_rt) g_rt->setFire(held != 0); }

void dionite_dash(void) noexcept {
    if (!g_rt) return;
    g_rt->setDash(true);
    g_releaseDash = true;
}

void dionite_ability(int32_t slot) noexcept {
    if (!g_rt || slot < 0 || slot >= DI_MAX_ABILITIES) return;
    g_rt->setAbility((int)slot, true);
    g_releaseAbility[slot] = true;
}

void dionite_move_to(float x, float y, float z) noexcept {
    if (g_rt) g_rt->clickToMove(dionite::math::Vec3(x, y, z));
}

void dionite_camera_pan(float deltaDeg) noexcept {
    if (g_rt) g_rt->cameraPan(deltaDeg);
}

void dionite_interact(void) noexcept { if (g_rt) g_rt->interact(); }
void dionite_use_potion(void) noexcept { if (g_rt) g_rt->usePotion(); }

// -- Meta / flow -------------------------------------------------------------

void dionite_respawn(void) noexcept { if (g_rt) g_rt->respawn(); }
void dionite_travel(int32_t region) noexcept { if (g_rt) g_rt->travel((int)region); }

void dionite_spire(int32_t enter) noexcept {
    if (!g_rt) return;
    if (enter) {
        g_rt->startSpire();
    } else {
        g_rt->exitSpire();
    }
}

void dionite_equip(int32_t index) noexcept { if (g_rt) g_rt->equipItem((int)index); }

void dionite_save(void) noexcept {
    try {
        if (g_rt) g_rt->saveNow();
    } catch (...) {
        setError("dionite_save: unknown failure");
    }
}

// -- Frame -------------------------------------------------------------------

const DIFrame* dionite_frame(void) noexcept {
    try {
        refreshFrame();
    } catch (...) {
        setError("dionite_frame: unknown failure");
        g_frame.hud = nullptr;
    }
    return &g_frame;
}

const char* dionite_hud_text(int32_t field) noexcept {
    if (g_rt && !g_hudValid) refreshHud();
    if (!g_hudValid) return "";
    switch (field) {
        case DI_HUD_TEXT_CLASS:           return g_hud.className;
        case DI_HUD_TEXT_RESOURCE:        return g_hud.resourceName;
        case DI_HUD_TEXT_REGION:          return g_hud.regionName;
        case DI_HUD_TEXT_QUEST_TITLE:     return g_hud.questTitle;
        case DI_HUD_TEXT_QUEST_OBJECTIVE: return g_hud.questObjective;
        case DI_HUD_TEXT_BOSS:            return g_hud.bossName;
        case DI_HUD_TEXT_PROMPT:          return g_hud.prompt;
        default:                          return "";
    }
}

const char* dionite_ability_name(int32_t slot) noexcept {
    if (slot < 0 || slot >= DI_MAX_ABILITIES) return "";
    if (g_rt && !g_hudValid) refreshHud();
    return g_hud.abilities[slot].name;
}

uint32_t dionite_text_revision(void) noexcept { return g_textRevision; }

int32_t dionite_pop_events(DIEvent* out, int32_t maxCount) noexcept {
    if (!g_rt || !out || maxCount <= 0) return 0;
    int32_t written = 0;
    try {
        DIEvent event{};
        while (written < maxCount && g_rt->popEvent(event)) {
            out[written++] = event;
            event = DIEvent{};
        }
    } catch (...) {
        setError("dionite_pop_events: unknown failure");
    }
    return written;
}

int32_t dionite_screen_to_world(float ndcX, float ndcY, float* outWorld3) noexcept {
    if (!g_rt || !outWorld3) return 0;
    dionite::math::Vec3 world;
    if (!g_rt->screenToWorld(ndcX, ndcY, world)) return 0;
    outWorld3[0] = world.x;
    outWorld3[1] = world.y;
    outWorld3[2] = world.z;
    return 1;
}

// -- Inventory ---------------------------------------------------------------

int32_t dionite_inventory_count(void) noexcept {
    return g_rt ? g_rt->inventoryCount() : 0;
}

int32_t dionite_inventory_item(int32_t index, DIItem* out) noexcept {
    if (!g_rt || !out) return 0;
    return g_rt->inventoryItem((int)index, *out) ? 1 : 0;
}

// -- Status ------------------------------------------------------------------

int32_t dionite_skill_points(void) noexcept { return g_rt ? g_rt->skillPoints() : 0; }
int32_t dionite_spire_best(void) noexcept { return g_rt ? g_rt->spireFloorBest() : 0; }
float dionite_play_seconds(void) noexcept { return g_rt ? g_rt->playSeconds() : 0.f; }

int32_t dionite_campaign_complete(void) noexcept {
    return (g_rt && g_rt->campaignComplete()) ? 1 : 0;
}

int32_t dionite_resumed_save(void) noexcept {
    return (g_rt && g_rt->loadedFromSave()) ? 1 : 0;
}

int32_t dionite_save_exists(const char* saveDir) noexcept {
    if (!saveDir || !*saveDir) return 0;
    std::string path(saveDir);
    if (path.back() != '/') path += '/';
    path += "dionite_save.json";
    std::ifstream in(path, std::ios::binary);
    return in.good() ? 1 : 0;
}

int32_t dionite_save_json(char* out, int32_t capacity) noexcept {
    if (out && capacity > 0) out[0] = '\0';
    if (!g_rt) return 0;
    try {
        g_saveJson = g_rt->saveJson();
    } catch (...) {
        setError("dionite_save_json: serialisation failed");
        return 0;
    }
    const size_t length = g_saveJson.size();
    if (out && capacity > 0) {
        const size_t copy = std::min(length, (size_t)capacity - 1);
        std::memcpy(out, g_saveJson.data(), copy);
        out[copy] = '\0';
    }
    return (int32_t)length;
}

// -- Audio -------------------------------------------------------------------

int32_t dionite_audio_drain(int32_t* outIds, float* outGains, float* outPitches,
                            float* outPans, int32_t maxEvents) noexcept {
    if (!g_rt || !outIds || !outGains || !outPitches || !outPans || maxEvents <= 0) {
        return 0;
    }
    int32_t written = 0;
    dionite::audio::AudioEvent event;
    while (written < maxEvents && g_rt->audio().poll(event)) {
        outIds[written] = (int32_t)event.id;
        outGains[written] = event.gain;
        outPitches[written] = event.pitch;
        outPans[written] = event.pan;
        ++written;
    }
    return written;
}

int32_t dionite_audio_pending(void) noexcept {
    return g_rt ? (int32_t)g_rt->audio().pending() : 0;
}

int32_t dionite_audio_music(void) noexcept {
    return g_rt ? (int32_t)g_rt->audio().music() : 0;
}

int32_t dionite_audio_ambient(void) noexcept {
    return g_rt ? (int32_t)g_rt->audio().ambient() : 0;
}

int32_t dionite_audio_in_combat(void) noexcept {
    return (g_rt && g_rt->audio().inCombat()) ? 1 : 0;
}

float dionite_audio_sfx_gain(void) noexcept {
    return g_rt ? g_rt->audio().sfxGain() : 0.f;
}

float dionite_audio_music_gain(void) noexcept {
    return g_rt ? g_rt->audio().musicGain() : 0.f;
}

void dionite_audio_set_master(float value) noexcept {
    if (g_rt) g_rt->audio().setMaster(value);
}

void dionite_audio_set_music_volume(float value) noexcept {
    if (g_rt) g_rt->audio().setMusicVolume(value);
}

void dionite_audio_set_sfx_volume(float value) noexcept {
    if (g_rt) g_rt->audio().setSfxVolume(value);
}

void dionite_audio_set_muted(int32_t muted) noexcept {
    if (g_rt) g_rt->audio().setMuted(muted != 0);
}

float dionite_audio_master(void) noexcept {
    return g_rt ? g_rt->audio().master() : 0.9f;
}

float dionite_audio_music_volume(void) noexcept {
    return g_rt ? g_rt->audio().musicVolume() : 0.7f;
}

float dionite_audio_sfx_volume(void) noexcept {
    return g_rt ? g_rt->audio().sfxVolume() : 1.f;
}

int32_t dionite_audio_is_muted(void) noexcept {
    return (g_rt && g_rt->audio().muted()) ? 1 : 0;
}

// -- Development seams -------------------------------------------------------

void dionite_dev_unlock_all(void) noexcept {
    if (g_rt) g_rt->devUnlockAllRegions();
}

void dionite_dev_set_boss_health(float fraction01) noexcept {
    if (g_rt) g_rt->devSetBossHealth(fraction01);
}

} // extern "C"
