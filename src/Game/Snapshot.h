// ============================================================================
// Dionite — Snapshot: pure-C data contract between the C++ game runtime and
// the platform renderer / HUD (Swift on iOS, anything else elsewhere).
//
// This header MUST remain valid C99 and valid C++ — it is included from
// Swift through the Objective-C bridging header, from the C++ runtime, and
// from the extern "C" bridge implementation. Keep it POD-only.
// ============================================================================
#ifndef DIONITE_SNAPSHOT_H
#define DIONITE_SNAPSHOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DI_MAX_ABILITIES 6
#define DI_MAX_INVENTORY 60
#define DI_MAX_DAMAGE_NUMBERS 64

// -- Mesh ids the platform renderer must provide ----------------------------
// Local-space conventions (the runtime and the renderer must agree):
//   QUAD      y = 0 plane, x/z in [-0.5, 0.5]      scale = (w, 1, h)
//   BOX       x/z in [-0.5, 0.5], y in [0, 1]      scale = (w, h, d), sits on pos.y
//   CAPSULE   y in [0, 1], radius 0.35 * sx         scale = (w, h, d), feet at pos.y
//   SPHERE    centered at origin, radius 0.5        pos = center
//   CONE      y in [0, 1], base radius 0.5          scale = (w, h, d), base at pos.y
//   CYLINDER  y in [0, 1], radius 0.5               scale = (w, h, d), base at pos.y
typedef enum DIMesh {
    DI_MESH_QUAD = 0,
    DI_MESH_BOX = 1,
    DI_MESH_CAPSULE = 2,
    DI_MESH_SPHERE = 3,
    DI_MESH_CONE = 4,
    DI_MESH_CYLINDER = 5,
    DI_MESH_COUNT = 6
} DIMesh;

// -- Instance flags ---------------------------------------------------------
#define DI_FLAG_BILLBOARD 0x1u // quad faces the camera instead of lying flat
#define DI_FLAG_UNLIT     0x2u // skip lighting, output color * emissive boost

// -- One drawable instance --------------------------------------------------
typedef struct DIInstance {
    float    x, y, z;    // world position
    float    rotY;       // yaw rotation (radians)
    float    sx, sy, sz; // scale
    float    emissive;   // 0 = matte, >0 self-illumination strength
    uint32_t rgba;       // color, byte order R,G,B,A in memory (little-endian 0xAABBGGRR)
    uint32_t mesh;       // DIMesh
    uint32_t flags;      // DI_FLAG_*
    float    phase;      // animation phase (seconds), shader/UI may use
    uint32_t pad0;
    float    pad1, pad2, pad3;
} DIInstance; // exactly 64 bytes

// -- Camera + lighting ------------------------------------------------------
typedef struct DICamera {
    float view[16];        // column-major (matches Metal float4x4)
    float proj[16];        // column-major
    float pos[3];
    float time;            // seconds since boot
    float sunDir[3];       // points TOWARD the sun
    float sunIntensity;
    float sunColor[3];
    float ambient;         // hemisphere ambient strength
    float fogColor[3];
    float fogDensity;
    float exposure;
    float shake;           // current screen-shake amount (0..1)
    float pad0, pad1;
} DICamera;

// -- HUD --------------------------------------------------------------------
typedef struct DIHudAbility {
    char   name[24];
    float  cooldown;    // remaining seconds
    float  cooldownMax;
    uint32_t ready;     // 1 if castable now
    float  resourceCost;
} DIHudAbility;

typedef struct DIHud {
    float    health, healthMax;
    float    resource, resourceMax;
    float    xpFrac;         // 0..1 progress toward next level
    int32_t  level;
    int32_t  gold;
    int32_t  skillPoints;
    int32_t  region;         // 0..4 biome index
    int32_t  unlockedRegions;
    int32_t  kills;
    int32_t  spireFloor;     // 0 = not in spire
    int32_t  dead;           // 1 when player is dead
    int32_t  bossActive;
    int32_t  abilityCount;
    int32_t  potions;
    float    bossFrac;
    char     className[24];
    char     resourceName[16];
    char     regionName[40];
    char     questTitle[64];
    char     questObjective[112];
    char     bossName[40];
    char     prompt[96];     // contextual interaction prompt ("" if none)
    float    playerX, playerZ;
    float    bossX, bossZ;
    DIHudAbility abilities[DI_MAX_ABILITIES];
} DIHud;

// Identifiers for the string fields of DIHud, consumed through the
// dionite_hud_text() accessor (see DioniteAPI.h).
typedef enum DIHudTextField {
    DI_HUD_TEXT_CLASS = 0,
    DI_HUD_TEXT_RESOURCE,
    DI_HUD_TEXT_REGION,
    DI_HUD_TEXT_QUEST_TITLE,
    DI_HUD_TEXT_QUEST_OBJECTIVE,
    DI_HUD_TEXT_BOSS,
    DI_HUD_TEXT_PROMPT,
    DI_HUD_TEXT_COUNT
} DIHudTextField;

// -- Floating damage numbers ------------------------------------------------
typedef struct DIDamageNumber {
    float    x, y, z;
    float    value;
    float    age, maxAge;
    uint32_t rgba;
    uint32_t crit;
} DIDamageNumber;

// -- Toast / narrative events ----------------------------------------------
typedef enum DIEventType {
    DI_EVENT_NONE = 0,
    DI_EVENT_INFO,
    DI_EVENT_LOOT,
    DI_EVENT_LEVELUP,
    DI_EVENT_QUEST,
    DI_EVENT_BOSS,
    DI_EVENT_DANGER
} DIEventType;

typedef struct DIEvent {
    int32_t type;   // DIEventType
    char    text[160];
} DIEvent;

// -- Inventory rows ---------------------------------------------------------
typedef struct DIItem {
    char    name[48];
    char    detail[112]; // rolled affix summary
    int32_t rarity;      // 0..5 (Common..Mythic)
    int32_t ilvl;
    float   damage;
    int32_t equipped;    // 1 when equipped
    int32_t sockets;
} DIItem;

// -- Remote players (player hubs / presence) -------------------------------
#define DI_MAX_REMOTE_PLAYERS 16

typedef struct DIRemotePlayer {
    uint64_t id;        // account-stable player id
    char     name[24];
    int32_t  classId;   // 0..4
    int32_t  level;
    float    x, y, z;   // interpolated render position (world space)
    float    yaw;
} DIRemotePlayer;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DIONITE_SNAPSHOT_H
