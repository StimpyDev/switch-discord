#pragma once

#include <string>

struct AppConfig {
    std::string guild_id;
    std::string channel_id;

    std::string client_id;
    std::string client_secret;
    std::string redirect_uri;
    std::string refresh_token;
    std::string oauth_scopes = "identify guilds dm_channels.read";
};

bool load_config(AppConfig& out, std::string& error);
