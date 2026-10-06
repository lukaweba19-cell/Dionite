// ============================================================================
// Dionite — C ABI surface called from Swift through the Objective-C bridging
// header. Pure C: safe to include from the bridging header and from the C++
// bridge implementation (DioniteBridgeImpl.cpp).
// ============================================================================
#ifndef DIONITE_API_H
#define DIONITE_API_H

#include <stdint.h>
#include "Game/Snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

// -- Lifecycle --------------------------------------------------------------
// saveDir: writable directory for autosaves (iOS Documents dir, /tmp on CI).
// classId: 0 crusader, 1 necromancer, 2 sorcerer, 3 ranger, 4 monk.
// seed:    world seed; pass 0 for a fresh randomized campaign.
// hasSave: 1 = continue existing save found in saveDir, 0 = fresh campaign.
void dionite_boot(const char* saveDir, int32_t classId, uint64_t seed, int32_t hasSave);
void dionite_shutdown(void);
void dionite_tick(float dt);
void dionite_resize(int32_t width, int32_t height);
void dionite_pause(void);
void dionite_resume(void);
int32_t dionite_version(void);

// -- Input ------------------------------------------------------------------
void dionite_set_move(float x, float y);           // left stick / WASD, -1..1
void dionite_set_aim(float x, float y);            // right stick, -1..1
void dionite_set_fire(int32_t pressed);            // fire held
void dionite_set_dash(int32_t pressed);            // dash button
void dionite_set_ability(int32_t slot, int32_t pressed); // ability slot 0..5
void dionite_click_to_move(float x, float y, float z);   // world-space ground point
void dionite_camera_pan(float deltaDeg);           // two-finger yaw pan
void dionite_interact(void);                       // open chest / use portal
void dionite_use_potion(void);

// -- Game flow --------------------------------------------------------------
void dionite_respawn(void);                        // after death
void dionite_travel(int32_t region);               // fast travel to unlocked biome
void dionite_start_spire(void);                    // enter Infinity Spire endgame
void dionite_exit_spire(void);                     // leave spire back to campaign
void dionite_equip_item(int32_t inventoryIndex);
void dionite_save_now(void);

// -- Frame snapshots --------------------------------------------------------
int32_t dionite_camera(DICamera* out);                       // 1 on success
int32_t dionite_hud(DIHud* out);                             // 1 on success
const DIInstance* dionite_instances(int32_t* opaqueCount,
                                    int32_t* translucentCount,
                                    int32_t* additiveCount);
int32_t dionite_damage_numbers(DIDamageNumber* out, int32_t maxCount);
int32_t dionite_pop_event(DIEvent* out);                     // 1 if an event was queued
int32_t dionite_inventory_count(void);
int32_t dionite_inventory_item(int32_t index, DIItem* out);  // 1 on success

// ---------------------------------------------------------------------------
// Swift-friendly accessors
//
// C fixed-size arrays that live *inside* a struct do not import into Swift in
// a usable shape, so every matrix / vector / string crosses the boundary
// through either a plain `float*` return or a caller-provided buffer. The
// scalar fields of the structs above are read directly from Swift.
// ---------------------------------------------------------------------------

// 16 column-major floats forming (proj * view). Replaces the pair each call to
// dionite_camera(); the pointer stays valid until the next snapshot call.
const float* dionite_camera_view_proj(void);

// out[0..2] eye position, [3..5] sun direction, [6..8] sun colour,
// [9..11] fog colour.
void dionite_camera_vectors(float* out12);

// outRight3 / outUp3: the camera basis, used to orient billboarded quads.
void dionite_camera_basis(float* outRight3, float* outUp3);

// Projects a ground-plane point from NDC (-1..1, y up) into world space so a
// screen tap can drive click-to-move. Returns 1 on success.
int32_t dionite_screen_to_world(float ndcX, float ndcY, float* outWorld3);

// out[0] time, [1] sun intensity, [2] ambient, [3] fog density,
// [4] exposure, [5] screen shake.
void dionite_camera_factors(float* out6);

// HUD string fields (see DIHudTextField).
void dionite_hud_text(int32_t field, char* out, int32_t cap);

// One ability slot: name is copied into outName, the rest are scalars.
void dionite_ability(int32_t slot, char* outName, int32_t nameCap,
                     float* outCooldown, float* outCooldownMax,
                     uint32_t* outReady, float* outCost);

// Pops the oldest queued event; returns 1 when one was written.
int32_t dionite_pop_event_text(int32_t* outType, char* out, int32_t cap);

// Copies one inventory row; returns 1 on success.
int32_t dionite_inventory_row(int32_t index,
                              char* outName, int32_t nameCap,
                              char* outDetail, int32_t detailCap,
                              int32_t* outRarity, int32_t* outIlvl,
                              float* outDamage, int32_t* outEquipped,
                              int32_t* outSockets);

// Campaign state that is not part of DIHud.
int32_t dionite_skill_points(void);
int32_t dionite_spire_best(void);
float   dionite_play_seconds(void);
int32_t dionite_campaign_complete(void);
int32_t dionite_has_save(void);        // 1 when this session resumed a save
int32_t dionite_save_exists(const char* saveDir);

// Serialises the live save state as compact JSON for cloud upload (PUT
// /api/save). The pointer stays valid until the next Dionite call; read
// dionite_save_json_length() bytes from it. Returns "" when nothing booted.
const char* dionite_save_json(void);
int32_t dionite_save_json_length(void);

// See GameRuntime::devUnlockAllRegions / devSetBossHealth.
void dionite_dev_unlock_all(void);
void dionite_dev_set_boss_health(float fraction01);

// -- Audio ------------------------------------------------------------------
// The core queues sound cues during dionite_tick(); the platform synthesizer
// drains them once per frame. Poll from the main thread only (the queue is
// not thread-safe) and hand the events to your audio render thread through
// your own lock-free buffer.
// Returns 1 when an event was written, 0 when the queue is empty.
int32_t dionite_audio_poll(int32_t* outId, float* outGain,
                           float* outPitch, float* outPan);
int32_t dionite_audio_pending(void);   // events waiting to be drained

// Score state machine for the generative music engine. Raw values are the
// dionite::audio::MusicMood / Ambient enum ints (see Audio/AudioManager.h):
// mood 0 silent, 1 hub, 2 explore, 3 combat, 4 boss, 5 death, 6 spire, 7 victory;
// ambient 0 none, 1 forest, 2 ash, 3 crypt, 4 ice, 5 sky.
int32_t dionite_audio_music(void);
int32_t dionite_audio_ambient(void);
int32_t dionite_audio_in_combat(void);

// Effective mixer gains (0..1, master/bus already combined, 0 when muted).
float dionite_audio_sfx_gain(void);
float dionite_audio_music_gain(void);

// Player-facing mixer control (settings UI). Persisted with the save.
void dionite_audio_set_master(float v);
void dionite_audio_set_music_volume(float v);
void dionite_audio_set_sfx_volume(float v);
void dionite_audio_set_muted(int32_t muted);

// Current mixer state, for reflecting the settings UI (saved values may
// differ from the defaults after a load).
float   dionite_audio_master(void);
float   dionite_audio_music_volume(void);
float   dionite_audio_sfx_volume(void);
int32_t dionite_audio_is_muted(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DIONITE_API_H
