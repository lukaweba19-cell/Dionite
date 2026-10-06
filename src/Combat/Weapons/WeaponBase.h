// ============================================================================
// Dionite — Combat: Weapon base (modular guns + Fortnite-style mods)
// ============================================================================
#pragma once
#include "Core/Math/Vector.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace dionite::combat {

enum class WeaponKind { Pistol, SMG, Shotgun, Rifle, Launcher, Staff, Wand };

struct WeaponMod {
    std::string id;
    std::string name;
    std::string slot; // "barrel","scope","magazine","stock","muzzle","grip"
    float dmgMult = 1.f;
    float fireRateMult = 1.f;
    float spreadMult = 1.f;
    float magazineAdd = 0;
    float projSpeedMult = 1.f;
    std::string projVfx;
};

struct WeaponEnchantment {
    std::string id;
    std::string name;
    std::string description;
    // Hook executed on hit / on kill (see Enchantments.h for handlers)
    enum class Trigger { OnHit, OnKill, OnCrit, OnReload } trigger = Trigger::OnHit;
};

struct WeaponBase {
    std::string id;
    std::string displayName;
    WeaponKind kind = WeaponKind::Pistol;
    float damage = 0.f;
    float fireRate = 0.f;
    float projectileSpeed = 0.f;
    float spread = 0.f;
    int   magazineSize = 0;
    int   pellets = 1;
    float reloadTime = 0.f;
    std::string projColor;
    float burnDmg = 0.f;
    float slow = 0.f;
    float explodeDmg = 0.f;
    float explodeRadius = 0.f;
    std::vector<WeaponMod> mods;
    std::vector<WeaponEnchantment> enchantments;
    std::vector<std::string> socketedGems;
    std::vector<std::string> socketedRunes; // ordered

    float currentFireCd = 0.f;
    int   currentMagazine = 0;
    bool  reloading = false;
    float reloadTimer = 0.f;
};

class WeaponSystem {
public:
    // Fires the weapon if cooldown allows. Returns true if it fired.
    bool fire(WeaponBase& w, const math::Vec3& origin, const math::Vec3& dir,
              std::function<void(const math::Vec3&, const math::Vec3&, float, const std::string&)> spawnProj) {
        if (w.currentFireCd > 0 || w.reloading) return false;
        if (w.currentMagazine <= 0) { w.reloading = true; w.reloadTimer = w.reloadTime; return false; }

        float effDmg = w.damage; float effSpread = w.spread; float effSpeed = w.projectileSpeed;
        for (auto& m : w.mods) { effDmg *= m.dmgMult; effSpread *= m.spreadMult; effSpeed *= m.projSpeedMult; }

        int pellets = std::max(1, w.pellets);
        for (int i = 0; i < pellets; ++i) {
            math::Vec3 d = dir;
            float a = ((std::rand() % 1000) / 1000.f - 0.5f) * effSpread * 2.f;
            float cs = std::cos(a), sn = std::sin(a);
            math::Vec3 rd{ d.x * cs - d.z * sn, d.y, d.x * sn + d.z * cs };
            spawnProj(origin, rd.normalized() * effSpeed, effDmg, w.projColor);
        }
        w.currentFireCd = w.fireRate;
        w.currentMagazine -= 1;
        return true;
    }

    void update(WeaponBase& w, float dt) {
        if (w.currentFireCd > 0) w.currentFireCd -= dt;
        if (w.reloading) {
            w.reloadTimer -= dt;
            if (w.reloadTimer <= 0) {
                w.reloading = false;
                w.currentMagazine = w.magazineSize;
                for (auto& m : w.mods) w.currentMagazine += (int)m.magazineAdd;
            }
        }
    }

    static WeaponBase fromTemplate(const std::string& id);
};

} // namespace dionite::combat
