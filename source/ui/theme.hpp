#pragma once

#include <SDL2/SDL.h>

// Discord client dark theme (desktop, 2022+ tokens)
namespace theme {

inline SDL_Color bg_tertiary() {
    return {30, 31, 34, 255};
}
inline SDL_Color bg_secondary() {
    return {43, 45, 49, 255};
}
inline SDL_Color bg_primary() {
    return {49, 51, 56, 255};
}
inline SDL_Color bg_floating() {
    return {35, 36, 40, 255};
}
inline SDL_Color bg_selected() {
    return {63, 65, 71, 255};
}
inline SDL_Color bg_input() {
    return {56, 58, 64, 255};
}
inline SDL_Color server_icon_bg() {
    return {49, 51, 56, 255};
}
inline SDL_Color brand() {
    return {88, 101, 242, 255};
}
inline SDL_Color brand_hover() {
    return {71, 82, 196, 255};
}
inline SDL_Color header_primary() {
    return {242, 243, 245, 255};
}
inline SDL_Color text_normal() {
    return {219, 222, 225, 255};
}
inline SDL_Color text_muted() {
    return {148, 155, 164, 255};
}
inline SDL_Color text_channel_icon() {
    return {128, 132, 142, 255};
}
inline SDL_Color divider() {
    return {32, 34, 37, 255};
}
inline SDL_Color pill() {
    return {242, 243, 245, 255};
}
inline SDL_Color online() {
    return {35, 165, 90, 255};
}

// Legacy aliases used elsewhere
inline SDL_Color bg() {
    return bg_primary();
}
inline SDL_Color panel() {
    return bg_secondary();
}
inline SDL_Color sidebar() {
    return bg_tertiary();
}
inline SDL_Color accent() {
    return brand();
}
inline SDL_Color text_primary() {
    return header_primary();
}
inline SDL_Color chat_bg() {
    return bg_primary();
}

} // namespace theme
