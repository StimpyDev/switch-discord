#include "config.hpp"
#include "paths.hpp"

#include <cstdio>
#include <string>

namespace {

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
        --end;
    return s.substr(start, end - start);
}

bool parse_line(const std::string& line, AppConfig& cfg) {
    if (line.empty() || line[0] == '#' || line[0] == ';')
        return true;
    auto eq = line.find('=');
    if (eq == std::string::npos)
        return true;
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    if (key == "guild_id")
        cfg.guild_id = val;
    else if (key == "channel_id")
        cfg.channel_id = val;
    else if (key == "client_id")
        cfg.client_id = val;
    else if (key == "client_secret")
        cfg.client_secret = val;
    else if (key == "redirect_uri")
        cfg.redirect_uri = val;
    else if (key == "refresh_token")
        cfg.refresh_token = val;
    else if (key == "oauth_scopes")
        cfg.oauth_scopes = val;
    return true;
}

} // namespace

bool load_config(AppConfig& out, std::string& error) {
    const std::string path = paths::config_ini();
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        error = "Missing config.ini in switch/switchcord/ (or legacy switchdiscord/)";
        return false;
    }

    AppConfig cfg{};
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        parse_line(line, cfg);
    }
    fclose(f);

    if (cfg.client_id.empty()) {
        error = "config.ini: client_id is required";
        return false;
    }
    if (cfg.redirect_uri.empty()) {
        error = "config.ini: redirect_uri is required (OAuth relay URL)";
        return false;
    }

    out = std::move(cfg);
    return true;
}
