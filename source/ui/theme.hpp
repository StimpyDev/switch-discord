#pragma once

#include <SDL2/SDL.h>

namespace theme {

inline SDL_Color bg() {
    return {54, 57, 63, 255};
}
inline SDL_Color panel() {
    return {47, 49, 54, 255};
}
inline SDL_Color sidebar() {
    return {32, 34, 37, 255};
}
inline SDL_Color accent() {
    return {88, 101, 242, 255};
}
inline SDL_Color text_primary() {
    return {220, 221, 222, 255};
}
inline SDL_Color text_muted() {
    return {114, 118, 125, 255};
}
inline SDL_Color chat_bg() {
    return {54, 57, 63, 255};
}

} // namespace theme
