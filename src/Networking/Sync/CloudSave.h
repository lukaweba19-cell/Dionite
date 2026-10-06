// ============================================================================
// Dionite — Networking: Cloud save (push/pull player profile)
// ============================================================================
#pragma once
#include "Networking/Client/NetworkClient.h"
#include <nlohmann/json.hpp>

namespace dionite::net {

class CloudSave {
public:
    explicit CloudSave(NetworkClient& nc) : nc_(nc) {}
    bool pull(nlohmann::json& out) {
        auto resp = nc_.get("/api/save");
        if (resp.statusCode != 200) return false;
        try { out = nlohmann::json::parse(resp.body); } catch (...) { return false; }
        return true;
    }
    bool push(const nlohmann::json& blob) {
        auto resp = nc_.put("/api/save", blob.dump());
        return resp.statusCode == 200;
    }
private:
    NetworkClient& nc_;
};

} // namespace dionite::net
