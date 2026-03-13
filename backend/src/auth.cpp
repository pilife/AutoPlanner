#include "auth.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#endif

// Base64url decode (for JWT parsing)
static std::string base64UrlDecode(const std::string& input) {
    std::string base64 = input;
    // Convert base64url to standard base64
    for (auto& c : base64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // Pad with '='
    while (base64.size() % 4 != 0) base64 += '=';

    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    int val = 0, bits = -8;
    for (unsigned char c : base64) {
        if (c == '=') break;
        auto pos = chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return decoded;
}

// Decode the payload of a JWT (second segment) without signature verification
static std::optional<nlohmann::json> decodeJwtPayload(const std::string& token) {
    // JWT format: header.payload.signature
    auto first = token.find('.');
    if (first == std::string::npos) return std::nullopt;
    auto second = token.find('.', first + 1);
    if (second == std::string::npos) return std::nullopt;

    std::string payload = token.substr(first + 1, second - first - 1);
    try {
        std::string decoded = base64UrlDecode(payload);
        return nlohmann::json::parse(decoded);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<MsGraphUser> verifyMicrosoftToken(const std::string& accessToken,
                                                  const std::string& idToken) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    // Preferred: verify via MS Graph API
    httplib::SSLClient cli("graph.microsoft.com");
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + accessToken}
    };

    auto result = cli.Get("/v1.0/me", headers);
    if (result && result->status == 200) {
        try {
            auto j = nlohmann::json::parse(result->body);
            MsGraphUser user;
            user.id = j.value("id", "");
            user.email = j.value("mail", "");
            if (user.email.empty()) {
                user.email = j.value("userPrincipalName", "");
            }
            user.display_name = j.value("displayName", "");
            if (!user.id.empty()) return user;
        } catch (...) {}
    }
#endif

    // Fallback: decode the ID token (JWT) to extract user claims
    // This works without SSL but doesn't verify the signature
    if (!idToken.empty()) {
        auto payload = decodeJwtPayload(idToken);
        if (payload) {
            MsGraphUser user;
            user.id = payload->value("oid", "");
            if (user.id.empty()) user.id = payload->value("sub", "");
            user.email = payload->value("preferred_username", "");
            if (user.email.empty()) user.email = payload->value("email", "");
            user.display_name = payload->value("name", "");
            if (!user.id.empty()) return user;
        }
    }

    return std::nullopt;
}

std::string generateSessionToken() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::ostringstream oss;
    for (int i = 0; i < 32; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return oss.str();
}

std::optional<int> getAuthenticatedUserId(Database& db, const httplib::Request& req) {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return std::nullopt;

    const std::string& header = it->second;
    if (header.substr(0, 7) != "Bearer ") return std::nullopt;

    std::string token = header.substr(7);
    return db.validateSession(token);
}
