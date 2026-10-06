// ============================================================================
// Dionite — Combat: Weapon enchantments (vampiric, echo-cast, chain-lightning, etc.)
// ============================================================================
#pragma once
#include "Combat/Weapons/WeaponBase.h"
#include <unordered_map>
#include <functional>

namespace dionite::combat {

class EnchantmentLibrary {
public:
    static EnchantmentLibrary& instance() {
        static EnchantmentLibrary lib;
        return lib;
    }

    using Handler = std::function<void(WeaponBase&, float dmgDealt, const math::Vec3& hitPos)>;

    void registerEnch(const std::string& id, Handler h) { handlers_[id] = std::move(h); }
    Handler get(const std::string& id) const {
        auto it = handlers_.find(id);
        return it == handlers_.end() ? Handler{} : it->second;
    }

    EnchantmentLibrary() {
        registerEnch("vampiric", [](WeaponBase&, float, const math::Vec3&) {
            // hook calls player heal in combat system
        });
        registerEnch("echo_cast", [](WeaponBase&, float, const math::Vec3&) {
            // 25% chance to re-fire after a short delay (handled in combat system)
        });
        registerEnch("chain_lightning", [](WeaponBase&, float, const math::Vec3&) {
            // chain to up to 3 nearby enemies
        });
        registerEnch("life_on_kill", [](WeaponBase&, float, const math::Vec3&) {});
        registerEnch("explosive_rounds", [](WeaponBase&, float, const math::Vec3&) {});
    }

private:
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace dionite::combat
