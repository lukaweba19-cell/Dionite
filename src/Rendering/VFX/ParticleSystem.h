// ParticleSystem.h — GPU-instanced particle simulation for spell impacts, trails, ambience.
// Pool size: 100k particles per frame target on iPhone 13+; sub-stages for emit / simulate /
// sort / draw. Each emitter is a data block; the renderer batches by texture+blend mode.
#pragma once
#include "Core/Math/Vector.h"
#include <string>
#include <vector>

namespace dionite::vfx {
using math::Vec3;
using math::Vec4;

enum class BlendMode { Alpha, Additive, Multiply, PremultipliedAlpha };
enum class EmitShape { Point, Sphere, Cone, Box, Ring, Mesh };

struct ColorOverLife {
    Vec4 start{1,1,1,1};
    Vec4 mid  {1,1,1,1};
    Vec4 end  {1,1,1,0};
};

struct SizeOverLife { float start = 1.f; float mid = 1.2f; float end = 0.f; };

struct ParticleEmitter {
    std::string  name;
    std::string  texture;          // PBR texture / sprite sheet path
    BlendMode    blend = BlendMode::Additive;
    EmitShape    shape = EmitShape::Sphere;
    float        emissionRate    = 200.f;     // particles per second
    int          burst           = 0;         // one-shot burst count
    float        lifetimeMin     = 0.6f;
    float        lifetimeMax     = 1.2f;
    Vec3         spawnVelocity   {0,2,0};
    Vec3         velocityRandom  {1.5f,1.5f,1.5f};
    Vec3         gravity         {0,-2.f,0};
    float        drag            = 0.4f;
    float        rotationRandom  = 6.28f;
    float        angularVel      = 1.5f;
    ColorOverLife color;
    SizeOverLife  size;
    bool         worldSpace      = true;
    bool         lightCasting    = false;     // each particle = mini point light? (for fire)
    Vec4         pointLightColor {1.0f, 0.55f, 0.18f, 0.6f}; // RGB + intensity
    float        pointLightRadius = 1.5f;

    // Sprite sheet animation (e.g., 4x4 explosion flipbook).
    int   flipbookCols = 1;
    int   flipbookRows = 1;
    float flipbookFps  = 24.f;
};

struct Particle {
    Vec3  position{0,0,0};
    Vec3  velocity{0,0,0};
    Vec4  color{1,1,1,1};
    float age  = 0.f;
    float life = 1.f;
    float size = 1.f;
    float rot  = 0.f;
    int   emitter = -1;
};

class ParticleSystem {
public:
    static constexpr size_t kMaxParticles = 100000;

    std::vector<ParticleEmitter> emitters;
    std::vector<Particle>        particles;

    int  registerEmitter(const ParticleEmitter& e);
    void spawn(int emitterIndex, const Vec3& origin, int count = -1);
    void update(float dt);
    void clear();
    size_t aliveCount() const { return particles.size(); }
};

} // namespace dionite::vfx
