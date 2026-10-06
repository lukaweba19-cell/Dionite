// ============================================================================
// Dionite — Auth: client-side token storage + Apple ID glue
// ============================================================================
#pragma once
#include "Networking/Client/NetworkClient.h"
#include <nlohmann/json.hpp>

namespace dionite::net {

struct AuthCredentials {
    std::string userId;
    std::string email;
    std::string accessToken;
    std::string refreshToken;
    std::string role;
};

class Auth {
public:
    explicit Auth(NetworkClient& nc) : nc_(nc) {}

    bool registerUser(const std::string& email, const std::string& password, AuthCredentials& out) {
        nlohmann::json body = {{"email", email}, {"password", password}};
        auto resp = nc_.post("/api/auth/register", body.dump());
        return parse(resp.body, out);
    }
    bool loginUser(const std::string& email, const std::string& password, AuthCredentials& out) {
        nlohmann::json body = {{"email", email}, {"password", password}};
        auto resp = nc_.post("/api/auth/login", body.dump());
        return parse(resp.body, out);
    }

    // Apple Sign-In requires native bridge — see platforms/ios/AppleAuth.swift
    bool appleSignIn(const std::string& identityToken, const std::string& authCode, AuthCredentials& out) {
        nlohmann::json body = {{"identity_token", identityToken}, {"auth_code", authCode}};
        auto resp = nc_.post("/api/auth/apple", body.dump());
        return parse(resp.body, out);
    }

private:
    bool parse(const std::string& body, AuthCredentials& out) {
        try {
            auto j = nlohmann::json::parse(body);
            out.userId = j.value("id", "");
            out.email = j.value("email", "");
            out.accessToken = j.value("access_token", "");
            out.refreshToken = j.value("refresh_token", "");
            out.role = j.value("role", "player");
            return !out.accessToken.empty();
        } catch (...) { return false; }
    }
    NetworkClient& nc_;
};

} // namespace dionite::net
