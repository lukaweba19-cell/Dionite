// ============================================================================
// Dionite — C ABI (version 2).
//
// The complete surface between the Metal/UIKit host and the portable C++ game
// core. Rules:
//
//   * POD-only. No C++ types cross this header.
//   * Every symbol is `extern "C"`, main-thread only, and never throws.
//   * Per-frame reads go through `dionite_frame()` and the stable string
//     accessors. None of them allocate; all pointers are owned by the runtime
//     and stay valid until the next tick/frame/shutdown.
//   * Buttons that the simulation consumes on a rising edge (`dionite_dash`,
//     `dionite_ability`) are one-shot calls, not held state.
//
// The header is valid C11 and C++17 so it can be included from the Obj-C
// bridging header and from the C++ implementation.
// ============================================================================
#ifndef DIONITE_API_H
#define DIONITE_API_H

#include <stdint.h>

#include "Game/Snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIONITE_ABI_VERSION 2

#ifdef __cplusplus
#define DI_NOEXCEPT noexcept
#else
#define DI_NOEXCEPT
#endif

// -- Lifecycle --------------------------------------------------------------

typedef struct DIBootParams {
    const char* saveDir;  // UTF-8 writable directory; copied before return
    uint64_t    seed;     // 0 = the deterministic default world seed
    int32_t     classId;  // 0..4 (see ClassRegistry)
    int32_t     hasSave;  // 1 = resume the save found in saveDir
    int32_t     width;    // drawable width in pixels
    int32_t     height;   // drawable height in pixels
} DIBootParams;

// Returns 1 when the runtime is ready, 0 on failure (`dionite_last_error`).
// Safe to call again: a previous runtime is shut down first.
int32_t     dionite_boot(const DIBootParams* params) DI_NOEXCEPT;
void        dionite_shutdown(void) DI_NOEXCEPT;
int32_t     dionite_is_running(void) DI_NOEXCEPT;
int32_t     dionite_abi_version(void) DI_NOEXCEPT;
int32_t     dionite_save_version(void) DI_NOEXCEPT;
const char* dionite_last_error(void) DI_NOEXCEPT;  // "" when none

void dionite_tick(float dt) DI_NOEXCEPT;
void dionite_resize(int32_t width, int32_t height) DI_NOEXCEPT;
void dionite_set_paused(int32_t paused) DI_NOEXCEPT;

// -- Input ------------------------------------------------------------------

void dionite_move(float x, float y) DI_NOEXCEPT;   // -1..1
void dionite_aim(float x, float y) DI_NOEXCEPT;    // -1..1
void dionite_fire(int32_t held) DI_NOEXCEPT;
void dionite_dash(void) DI_NOEXCEPT;               // edge-triggered
void dionite_ability(int32_t slot) DI_NOEXCEPT;    // edge-triggered, 0..5
void dionite_move_to(float x, float y, float z) DI_NOEXCEPT;
void dionite_camera_pan(float deltaDeg) DI_NOEXCEPT;
void dionite_interact(void) DI_NOEXCEPT;
void dionite_use_potion(void) DI_NOEXCEPT;

// -- Meta / flow -------------------------------------------------------------

void dionite_respawn(void) DI_NOEXCEPT;
void dionite_travel(int32_t region) DI_NOEXCEPT;
void dionite_spire(int32_t enter) DI_NOEXCEPT;
void dionite_equip(int32_t inventoryIndex) DI_NOEXCEPT;
void dionite_save(void) DI_NOEXCEPT;

// -- Frame snapshot ----------------------------------------------------------

typedef struct DIFrame {
    const DICamera*        camera;
    const DIHud*           hud;
    const DIInstance*      instances;        // opaque, then translucent, then additive
    int32_t                opaqueCount;
    int32_t                translucentCount;
    int32_t                additiveCount;
    const DIDamageNumber*  damageNumbers;
    int32_t                damageNumberCount;
    const float*           viewProjection;   // 16 column-major floats, proj * view
} DIFrame;

// Refreshes and returns the frame snapshot. One call per rendered frame.
const DIFrame* dionite_frame(void) DI_NOEXCEPT;

// Stable, NUL-terminated strings. No allocation; pointers live in the runtime.
const char* dionite_hud_text(int32_t field) DI_NOEXCEPT;        // DIHudTextField
const char* dionite_ability_name(int32_t slot) DI_NOEXCEPT;     // "" when absent

// Increments whenever any HUD or ability string changes, so the host can cache
// Foundation strings instead of rebuilding them every frame.
uint32_t dionite_text_revision(void) DI_NOEXCEPT;

// Copies up to `maxCount` queued events, oldest first. Returns the count.
int32_t dionite_pop_events(DIEvent* out, int32_t maxCount) DI_NOEXCEPT;

// Projects a screen point (NDC, y up) onto the ground plane.
int32_t dionite_screen_to_world(float ndcX, float ndcY, float* outWorld3) DI_NOEXCEPT;

// -- Inventory ---------------------------------------------------------------

int32_t dionite_inventory_count(void) DI_NOEXCEPT;
int32_t dionite_inventory_item(int32_t index, DIItem* out) DI_NOEXCEPT;

// -- Status ------------------------------------------------------------------

int32_t dionite_skill_points(void) DI_NOEXCEPT;
int32_t dionite_spire_best(void) DI_NOEXCEPT;
float   dionite_play_seconds(void) DI_NOEXCEPT;
int32_t dionite_campaign_complete(void) DI_NOEXCEPT;
int32_t dionite_resumed_save(void) DI_NOEXCEPT;
int32_t dionite_save_exists(const char* saveDir) DI_NOEXCEPT;

// Copies the live save JSON into `out`, always NUL-terminated. Returns the full
// byte length (so `dionite_save_json(NULL, 0)` reports the required capacity).
int32_t dionite_save_json(char* out, int32_t capacity) DI_NOEXCEPT;

// -- Audio -------------------------------------------------------------------

// Drains up to `maxEvents` queued cues into the four parallel arrays. Each array
// must hold `maxEvents` elements. Returns the number of cues written.
int32_t dionite_audio_drain(int32_t* outIds, float* outGains, float* outPitches,
                            float* outPans, int32_t maxEvents) DI_NOEXCEPT;
int32_t dionite_audio_pending(void) DI_NOEXCEPT;
int32_t dionite_audio_music(void) DI_NOEXCEPT;
int32_t dionite_audio_ambient(void) DI_NOEXCEPT;
int32_t dionite_audio_in_combat(void) DI_NOEXCEPT;
float   dionite_audio_sfx_gain(void) DI_NOEXCEPT;
float   dionite_audio_music_gain(void) DI_NOEXCEPT;
void    dionite_audio_set_master(float value) DI_NOEXCEPT;
void    dionite_audio_set_music_volume(float value) DI_NOEXCEPT;
void    dionite_audio_set_sfx_volume(float value) DI_NOEXCEPT;
void    dionite_audio_set_muted(int32_t muted) DI_NOEXCEPT;
float   dionite_audio_master(void) DI_NOEXCEPT;
float   dionite_audio_music_volume(void) DI_NOEXCEPT;
float   dionite_audio_sfx_volume(void) DI_NOEXCEPT;
int32_t dionite_audio_is_muted(void) DI_NOEXCEPT;

// -- Development seams -------------------------------------------------------
// Compiled in every build; used by the automated verification harness.
void dionite_dev_unlock_all(void) DI_NOEXCEPT;
void dionite_dev_set_boss_health(float fraction01) DI_NOEXCEPT;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DIONITE_API_H
