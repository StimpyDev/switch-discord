#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

struct QrBitmap {
    int modules = 0;
    std::vector<uint8_t> dark;
};

bool qr_encode(const std::string& text, QrBitmap& out);

void qr_draw(SDL_Renderer* renderer, int x, int y, int pixel_size, const QrBitmap& qr,
             SDL_Color dark, SDL_Color light, int quiet_modules = 4);

} // namespace ui
