#pragma once

#include "config.hpp"

#include <string>

struct UserSession {
    std::string access_token;
    std::string refresh_token;
    long long expires_at = 0;
};

bool load_user_session(UserSession& out);
bool save_user_session(const UserSession& session);

// Returns valid bearer access token (refreshes if needed).
bool ensure_user_access_token(const AppConfig& config, UserSession& session, std::string& error);

std::string build_authorize_url(const AppConfig& config, const std::string& code_challenge,
                                const std::string& state);

struct OAuthPending {
    std::string authorize_url;
    std::string code_verifier;
    std::string switch_ip;
    int listen_fd = -1;
};

bool oauth_begin(const AppConfig& config, OAuthPending& pending, std::string& error);
bool oauth_wait_code(OAuthPending& pending, std::string& code, std::string& error);
void oauth_cancel(OAuthPending& pending);

bool oauth_exchange_code(const AppConfig& config, const std::string& code,
                         const std::string& code_verifier, UserSession& session,
                         std::string& error);
