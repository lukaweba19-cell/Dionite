// ============================================================================
// Dionite — Combat: Enemy AI state machine (Idle / Chase / Attack / Flee / Dead)
// ============================================================================
#pragma once
#include "Core/Math/Vector.h"
#include <string>
#include <functional>

namespace dionite::combat {

enum class AIState { Idle, Patrol, Chase, Attack, Flee, Dead };

struct EnemyStats {
    float maxHp = 50, hp = 50;
    float dmg = 8;
    float speed = 80;
    float aggroRange = 350;
    float attackRange = 60;
    float attackCd = 1.2f;
    float fireRate = 1.0f;
    bool  ranged = false;
    float projSpeed = 280;
    AIState state = AIState::Idle;
    float stateTimer = 0;
};

struct EnemyInstance {
    std::string typeId;
    EnemyStats stats;
    math::Vec3 position;
    math::Vec3 targetDir{1,0,0};
    float fireTimer = 0;
};

class EnemyAI {
public:
    // Returns true if the enemy attacked this frame (for VFX spawning).
    bool update(EnemyInstance& e, const math::Vec3& playerPos, float dt,
                std::function<void(const math::Vec3&, const math::Vec3&, float)> spawnProjectile) {
        if (e.stats.state == AIState::Dead) return false;
        math::Vec3 toPlayer = playerPos - e.position;
        float dist = toPlayer.length();
        if (e.stats.hp <= 0) { e.stats.state = AIState::Dead; return false; }

        if (dist <= e.stats.attackRange) {
            e.stats.state = AIState::Attack;
        } else if (dist <= e.stats.aggroRange) {
            e.stats.state = AIState::Chase;
        } else {
            e.stats.state = AIState::Idle;
        }

        if (e.stats.state == AIState::Chase) {
            math::Vec3 d = toPlayer.normalized();
            e.position += d * e.stats.speed * dt;
            e.targetDir = d;
        } else if (e.stats.state == AIState::Attack) {
            e.fireTimer -= dt;
            if (e.fireTimer <= 0) {
                e.fireTimer = e.stats.fireRate;
                if (e.stats.ranged) {
                    math::Vec3 d = toPlayer.normalized();
                    spawnProjectile(e.position, d * e.stats.projSpeed, e.stats.dmg);
                    return true;
                } else {
                    // melee — caller must check overlap + apply damage
                    return true;
                }
            }
        }
        return false;
    }
};

} // namespace dionite::combat
