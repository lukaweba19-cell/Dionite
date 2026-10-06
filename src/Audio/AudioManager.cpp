#include "AudioManager.h"

namespace dionite::audio {

void AudioManager::reset() {
    queue_.clear();
    mood_ = MusicMood::Silent;
    ambient_ = Ambient::None;
    inCombat_ = false;
}

void AudioManager::post(Sound id, float gain, float pitch, float pan) {
    if (id == Sound::None) return;
    // Drop the oldest event rather than growing without bound — during a
    // bullet-hell moment the newest sound is the one the player cares about.
    if (queue_.size() >= kMaxQueue) queue_.pop_front();
    AudioEvent e;
    e.id = id;
    e.gain = gain < 0.f ? 0.f : (gain > 1.f ? 1.f : gain);
    e.pitch = pitch < 0.5f ? 0.5f : (pitch > 2.f ? 2.f : pitch);
    e.pan = pan < -1.f ? -1.f : (pan > 1.f ? 1.f : pan);
    queue_.push_back(e);
}

bool AudioManager::poll(AudioEvent& out) {
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop_front();
    return true;
}

void AudioManager::setMusic(MusicMood mood) {
    if (mood_ == mood) return;
    mood_ = mood;
}

void AudioManager::setAmbient(Ambient bed) {
    if (ambient_ == bed) return;
    ambient_ = bed;
}

} // namespace dionite::audio
