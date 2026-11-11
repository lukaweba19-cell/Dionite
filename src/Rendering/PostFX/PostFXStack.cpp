#include "PostFXStack.h"

namespace dionite::postfx {

// Per-biome grading presets. Consumed by the Metal post chain (WS9).
void PostFXStack::loadBiomeProfile(const std::string& id) {
    profile = PostFXProfile{};
    if (id == "blackthorn_forest") {
        profile.grading.lutTexture     = "blackthorn_dusk";
        profile.grading.saturation     = 0.9f;
        profile.grading.contrast       = 1.10f;
        profile.tonemap.exposure       = 0.95f;
        profile.bloom.intensity        = 0.55f;
        profile.vignette.intensity     = 0.5f;
    } else if (id == "scorch_dunes") {
        profile.grading.lutTexture     = "scorch_noon";
        profile.grading.saturation     = 1.15f;
        profile.grading.contrast       = 1.05f;
        profile.tonemap.exposure       = 1.10f;
        profile.bloom.intensity        = 0.85f;
        profile.ca.strength            = 0.0045f;
    } else if (id == "frozen_spire") {
        profile.grading.lutTexture     = "frozen_blue";
        profile.grading.saturation     = 0.85f;
        profile.tonemap.exposure       = 1.0f;
        profile.bloom.intensity        = 0.95f;
        profile.dof.enabled            = true;
    } else if (id == "ashen_marsh") {
        profile.grading.lutTexture     = "ashen_green";
        profile.grading.saturation     = 0.7f;
        profile.tonemap.exposure       = 0.9f;
        profile.bloom.intensity        = 0.4f;
        profile.vignette.intensity     = 0.6f;
    } else if (id == "infinity_spire") {
        profile.grading.lutTexture     = "spire_neon";
        profile.grading.saturation     = 1.25f;
        profile.tonemap.exposure       = 1.15f;
        profile.bloom.intensity        = 1.10f;
        profile.ca.strength            = 0.006f;
    }
}

} // namespace dionite::postfx
