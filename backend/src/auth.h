#pragma once

#include <string>
#include <optional>
#include <functional>
#include <httplib.h>
#include "database.h"

struct MsGraphUser {
    std::string id;
    std::string email;
    std::string display_name;
};

// Verify a Microsoft token. Tries MS Graph /me with accessToken first (requires SSL),
// falls back to decoding the idToken JWT payload (works without SSL).
std::optional<MsGraphUser> verifyMicrosoftToken(const std::string& accessToken,
                                                  const std::string& idToken = "");

// Generate a random 32-char hex session token
std::string generateSessionToken();

// Extract user_id from Authorization: Bearer <token> header
std::optional<int> getAuthenticatedUserId(Database& db, const httplib::Request& req);

// Helper: wrap a handler to require authentication
using AuthHandler = std::function<void(const httplib::Request&, httplib::Response&, int userId)>;

inline auto requireAuth(Database& db, AuthHandler handler) {
    return [&db, handler](const httplib::Request& req, httplib::Response& res) {
        auto userId = getAuthenticatedUserId(db, req);
        if (!userId) {
            res.status = 401;
            res.set_content(R"({"error":"Unauthorized"})", "application/json");
            return;
        }
        handler(req, res, *userId);
    };
}
