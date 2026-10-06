// PostFXStack.h — Full-screen post-processing pipeline (mobile-tuned).
// Order of operations matches "Diablo IV" presentation: HDR → Bloom → Auto-exposure →
// Color grading (3D LUT) → Tonemap (ACES) → Chromatic aberration (subtle) → Film grain →
// Vignette. Configured per-biome via VisualProfile. Implemented as Metal compute kernels
// for iOS, fragment shaders for the desktop/Android validation path.
#pragma once
#include "Core/Math/Vector.h"
#include <string>

namespace dionite::postfx {
using math::Vec3;
using math::Vec4;

struct BloomConfig {
    bool  enabled         = true;
    float threshold       = 1.05f;
    float softKnee        = 0.5f;
    float intensity       = 0.7f;
    int   mipLevels       = 6;
    float scatter         = 0.7f;
};

struct ColorGrading {
    std::string lutTexture = "luts/dionite_filmic.cube";
    float       lutStrength = 0.85f;
    float       contrast    = 1.05f;
    float       saturation  = 1.08f;
    Vec3        liftRGB    {0,0,0};
    Vec3        gammaRGB   {1,1,1};
    Vec3        gainRGB    {1,1,1};
};

struct Tonemap {
    enum Mode { ACES, Filmic, Reinhard, Neutral };
    Mode  mode      = ACES;
    float exposure  = 1.0f;
    float whitePoint = 11.2f;
};

struct ChromaticAberration { bool enabled = true; float strength = 0.0035f; };
struct Vignette            { bool enabled = true; float intensity = 0.4f; float falloff = 0.35f; };
struct FilmGrain           { bool enabled = true; float intensity = 0.05f; bool colored = false; };

struct MotionBlur          { bool enabled = false; float maxRadius = 8.f; }; // off on mobile
struct ScreenSpaceReflections { bool enabled = false; int steps = 24; float thickness = 0.4f; };
struct ScreenSpaceAO        { bool enabled = true;  float radius = 0.5f; float intensity = 0.8f; int samples = 12; };

struct DepthOfField {
    bool enabled = true; float aperture = 4.0f; float focusDistance = 6.5f;
    float focusRange = 2.0f; float maxBlur = 0.012f;
};

struct PostFXProfile {
    std::string         name = "dionite_default";
    BloomConfig         bloom;
    ColorGrading        grading;
    Tonemap             tonemap;
    ChromaticAberration ca;
    Vignette            vignette;
    FilmGrain           grain;
    MotionBlur          motionBlur;
    ScreenSpaceReflections ssr;
    ScreenSpaceAO       ssao;
    DepthOfField        dof;
};

class PostFXStack {
public:
    PostFXProfile profile;
    void loadBiomeProfile(const std::string& biomeId);  // swap LUTs + intensities
    void apply(/* framebuffer in/out + camera uniforms */);  // wired by Metal/Vulkan layer
};

} // namespace dionite::postfx
