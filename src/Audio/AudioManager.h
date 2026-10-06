// ============================================================================
// Dionite — Audio: real, event-driven audio core.
//
// The simulation never blocks on sound: every gameplay moment posts a Sound
// into a bounded queue and the platform synthesizer (AudioEngine.swift on
// iOS — a procedural AVAudioSourceNode synth, so the game ships with zero
// audio assets) drains it once per frame. Music/ambient state is a small
// state machine the runtime drives (Hub / Explore / Combat / Boss / Death /
// Spire / Victory) so the platform can morph its generative score.
//
// Spatialisation happens here: postAt() weighs distance against the camera
// and pans with the camera yaw, because the core knows where everything is.
// ============================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <string>

namespace dionite::audio {

/// Every sound the game can produce. Values are stable — the platform
/// synthesizer maps them to waveform recipes.
enum class Sound : int32_t {
    None = 0,
    // -- weapons / combat -------------------------------------------------
    GunShot,        // light weapon report
    GunShotHeavy,   // shotgun / launcher
    Swing,          // melee / staff swoosh
    BowShot,        // string snap
    Reload,         // magazine click
    Impact,         // bullet hits flesh/stone
    CritHit,        // crunchy crit confirmation
    EnemyHit,
    EnemyDie,
    BossDie,
    BossRoar,       // intro + phase change
    PlayerHurt,
    PlayerDeath,
    // -- abilities --------------------------------------------------------
    CastFire,
    CastFrost,
    CastShock,
    CastHoly,
    CastShadow,
    Dash,
    Heal,
    Ultimate,
    // -- world / interaction ----------------------------------------------
    ChestOpen,
    MimicBite,
    GoldPickup,
    ItemPickup,
    LegendaryDrop,  // orange beam sting
    PotionDrink,
    Portal,
    DoorOpen,
    Shrine,
    LevelUp,
    QuestAccept,
    QuestComplete,
    SkillLearn,
    CraftSuccess,
    CraftFail,
    BuyItem,
    SellItem,
    SocketGem,
    RuneWord,
    // -- UI ---------------------------------------------------------------
    UIClick,
    UIError,
    TabSwitch,
    // -- events / meta ----------------------------------------------------
    WorldEvent,
    Thunder,
    SpireFloor,
    GhostSpawn,
    Victory,        // campaign completion sting
    Count
};

struct AudioEvent {
    Sound id = Sound::None;
    float gain = 1.f;    // 0..1, already distance-attenuated by the core
    float pitch = 1.f;   // 0.5..2, per-instance variation
    float pan = 0.f;     // -1 (left) .. +1 (right), camera-relative
};

/// What the generative score is doing right now.
enum class MusicMood : int32_t {
    Silent = 0,
    Hub,        // warm, slow, safe
    Explore,    // eerie wandering
    Combat,     // driving
    Boss,       // oppressive
    Death,      // dirge
    Spire,      // dissonant climb
    Victory     // major-key release
};

/// Biome ambience bed (wind / water / fire crackle mix chosen per biome).
enum class Ambient : int32_t {
    None = 0,
    Forest,
    Ash,
    Crypt,
    Ice,
    Sky
};

/// Owned by GameRuntime. Platform-agnostic: no device, no thread — just the
/// truth about what should be audible, drained by the platform each frame.
class AudioManager {
public:
    void reset();

    // -- one-shot queue ----------------------------------------------------
    void post(Sound id, float gain = 1.f, float pitch = 1.f, float pan = 0.f);
    /// Pops the oldest pending event. Returns false when the queue is empty.
    bool poll(AudioEvent& out);
    size_t pending() const { return queue_.size(); }

    // -- score state machine ----------------------------------------------
    void setMusic(MusicMood mood);
    void setAmbient(Ambient bed);
    void setInCombat(bool combat) { inCombat_ = combat; }
    MusicMood music() const { return mood_; }
    Ambient ambient() const { return ambient_; }
    bool inCombat() const { return inCombat_; }

    // -- mixer -------------------------------------------------------------
    void setMaster(float v) { master_ = clamp01(v); }
    void setMusicVolume(float v) { musicVol_ = clamp01(v); }
    void setSfxVolume(float v) { sfxVol_ = clamp01(v); }
    void setMuted(bool m) { muted_ = m; }
    float master() const { return master_; }
    float musicVolume() const { return musicVol_; }
    float sfxVolume() const { return sfxVol_; }
    bool muted() const { return muted_; }

    /// Effective gains the platform applies (already combined).
    float sfxGain() const { return muted_ ? 0.f : master_ * sfxVol_; }
    float musicGain() const { return muted_ ? 0.f : master_ * musicVol_; }

private:
    static float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    static constexpr size_t kMaxQueue = 96;
    std::deque<AudioEvent> queue_;

    MusicMood mood_ = MusicMood::Silent;
    Ambient ambient_ = Ambient::None;
    bool inCombat_ = false;

    float master_ = 0.9f;
    float musicVol_ = 0.7f;
    float sfxVol_ = 1.0f;
    bool muted_ = false;
};

} // namespace dionite::audio
