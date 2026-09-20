#pragma once

#include <cstdio>
#include <string>

namespace paths {

inline constexpr const char* kInstallDir = "sdmc:/switch/switchcord/";
inline constexpr const char* kLegacyDir = "sdmc:/switch/switchdiscord/";

inline bool exists(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f)
        return false;
    std::fclose(f);
    return true;
}

inline std::string pick(const char* filename) {
    std::string primary = std::string(kInstallDir) + filename;
    if (exists(primary.c_str()))
        return primary;
    return std::string(kLegacyDir) + filename;
}

inline std::string config_ini() {
    return pick("config.ini");
}

inline std::string auth_json() {
    return pick("auth.json");
}

inline std::string auth_json_save_path() {
    if (exists((std::string(kInstallDir) + "config.ini").c_str()) ||
        exists((std::string(kInstallDir) + "switchcord.nro").c_str()))
        return std::string(kInstallDir) + "auth.json";
    if (exists((std::string(kLegacyDir) + "config.ini").c_str()) ||
        exists((std::string(kLegacyDir) + "switchdiscord.nro").c_str()))
        return std::string(kLegacyDir) + "auth.json";
    return std::string(kInstallDir) + "auth.json";
}

} // namespace paths
