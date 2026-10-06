// ============================================================================
// Dionite / Shattered Wilds — Core ECS: System base + World/Registry
// ============================================================================
#pragma once
#include "Entity.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <algorithm>

namespace dionite::core {

class System {
public:
    virtual ~System() = default;
    virtual void onAttach() {}
    virtual void update(float dt) = 0;
    int priority = 0;
};

class World {
public:
    EntityID create() {
        EntityID id = nextId_++;
        entities_[id] = std::make_unique<Entity>(id);
        return id;
    }

    Entity* get(EntityID id) {
        auto it = entities_.find(id);
        return it == entities_.end() ? nullptr : it->second.get();
    }

    void destroy(EntityID id) { pendingDeletes_.push_back(id); }

    void addSystem(std::shared_ptr<System> sys) {
        sys->onAttach();
        systems_.push_back(std::move(sys));
        std::sort(systems_.begin(), systems_.end(),
                  [](auto& a, auto& b) { return a->priority < b->priority; });
    }

    void update(float dt) {
        for (auto& s : systems_) s->update(dt);
        for (auto id : pendingDeletes_) entities_.erase(id);
        pendingDeletes_.clear();
    }

    template <typename T>
    void forEach(std::function<void(Entity&, T&)> fn) {
        for (auto& [id, e] : entities_) {
            if (auto* c = e->template getComponent<T>()) fn(*e, *c);
        }
    }

    size_t entityCount() const { return entities_.size(); }

private:
    std::unordered_map<EntityID, std::unique_ptr<Entity>> entities_;
    std::vector<std::shared_ptr<System>> systems_;
    std::vector<EntityID> pendingDeletes_;
    EntityID nextId_ = 1;
};

} // namespace dionite::core
