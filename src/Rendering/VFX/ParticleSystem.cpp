#include "ParticleSystem.h"
#include "Core/Math/Random.h"
#include <algorithm>

namespace dionite::vfx {

static dionite::math::Random g_rng(0xD10417E5u);

int ParticleSystem::registerEmitter(const ParticleEmitter& e) {
    emitters.push_back(e);
    return (int)emitters.size() - 1;
}

void ParticleSystem::spawn(int idx, const Vec3& origin, int count) {
    if (idx < 0 || idx >= (int)emitters.size()) return;
    const auto& em = emitters[idx];
    int n = count >= 0 ? count : em.burst;
    if (n <= 0) return;
    for (int i = 0; i < n; ++i) {
        if (particles.size() >= kMaxParticles) break;
        Particle p;
        p.position = origin;
        p.velocity = {
            em.spawnVelocity.x + g_rng.rangeF(-em.velocityRandom.x, em.velocityRandom.x),
            em.spawnVelocity.y + g_rng.rangeF(-em.velocityRandom.y, em.velocityRandom.y),
            em.spawnVelocity.z + g_rng.rangeF(-em.velocityRandom.z, em.velocityRandom.z),
        };
        p.life    = g_rng.rangeF(em.lifetimeMin, em.lifetimeMax);
        p.age     = 0.f;
        p.size    = em.size.start;
        p.rot     = g_rng.rangeF(0.f, em.rotationRandom);
        p.color   = em.color.start;
        p.emitter = idx;
        particles.push_back(p);
    }
}

static Vec4 lerp4(const Vec4& a, const Vec4& b, float t) {
    return { a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t };
}

void ParticleSystem::update(float dt) {
    for (size_t i = 0; i < particles.size(); ) {
        Particle& p = particles[i];
        const ParticleEmitter& em = emitters[p.emitter];
        p.age += dt;
        if (p.age >= p.life) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }
        float n = p.age / std::max(0.001f, p.life);
        // Verlet-ish step with gravity + drag.
        p.velocity.x += em.gravity.x * dt;
        p.velocity.y += em.gravity.y * dt;
        p.velocity.z += em.gravity.z * dt;
        float damp = std::max(0.f, 1.f - em.drag * dt);
        p.velocity.x *= damp; p.velocity.y *= damp; p.velocity.z *= damp;
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;
        p.rot   += em.angularVel * dt;

        // Color / size over life with mid keyframe.
        if (n < 0.5f) {
            float k = n * 2.f;
            p.color = lerp4(em.color.start, em.color.mid, k);
            p.size  = em.size.start + (em.size.mid - em.size.start) * k;
        } else {
            float k = (n - 0.5f) * 2.f;
            p.color = lerp4(em.color.mid, em.color.end, k);
            p.size  = em.size.mid + (em.size.end - em.size.mid) * k;
        }
        ++i;
    }
}

void ParticleSystem::clear() { particles.clear(); }

} // namespace dionite::vfx
