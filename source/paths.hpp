#pragma once

#include <string>

namespace paths {

inline constexpr const char* kInstallDir = "sdmc:/switch/switchcord/";

inline std::string install_path(const char* filename) {
    return std::string(kInstallDir) + filename;
}

inline std::string config_ini() {
    return install_path("config.ini");
}

inline std::string auth_json() {
    return install_path("auth.json");
}

inline std::string auth_json_save_path() {
    return install_path("auth.json");
}

} // namespace paths
