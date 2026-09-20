#include "ui/primitives.hpp"

#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ui {

void set_color(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void fill_rect(SDL_Renderer* r, SDL_Rect rect, SDL_Color c) {
    set_color(r, c);
    SDL_RenderFillRect(r, &rect);
}

void fill_hline(SDL_Renderer* r, int x, int y, int w, SDL_Color c) {
    fill_rect(r, {x, y, w, 1}, c);
}

void fill_circle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c) {
    if (radius <= 0)
        return;
    set_color(r, c);
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
        SDL_Rect row{cx - dx, cy + dy, dx * 2 + 1, 1};
        SDL_RenderFillRect(r, &row);
    }
}

void fill_rounded(SDL_Renderer* r, SDL_Rect rect, int radius, SDL_Color c) {
    if (radius <= 0) {
        fill_rect(r, rect, c);
        return;
    }
    radius = std::min(radius, std::min(rect.w, rect.h) / 2);
    fill_rect(r, {rect.x + radius, rect.y, rect.w - 2 * radius, rect.h}, c);
    fill_rect(r, {rect.x, rect.y + radius, rect.w, rect.h - 2 * radius}, c);
    fill_circle(r, rect.x + radius, rect.y + radius, radius, c);
    fill_circle(r, rect.x + rect.w - radius - 1, rect.y + radius, radius, c);
    fill_circle(r, rect.x + radius, rect.y + rect.h - radius - 1, radius, c);
    fill_circle(r, rect.x + rect.w - radius - 1, rect.y + rect.h - radius - 1, radius, c);
}

SDL_Color avatar_color(const std::string& seed) {
    static const SDL_Color palette[] = {
        {88, 101, 242, 255},  {237, 66, 69, 255},   {250, 166, 26, 255},
        {67, 181, 129, 255},    {254, 231, 92, 255},  {235, 69, 158, 255},
        {58, 167, 255, 255},    {255, 115, 55, 255},  {131, 142, 255, 255},
    };
    size_t h = 5381;
    for (unsigned char ch : seed)
        h = ((h << 5) + h) + ch;
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

void draw_text(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y,
               SDL_Color color, int wrap_w) {
    if (!font || text.empty())
        return;
    SDL_Surface* surf = wrap_w > 0 ? TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, wrap_w)
                                   : TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf)
        return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

std::string avatar_initial(const std::string& display_name) {
    if (display_name.empty())
        return "?";
    size_t i = 0;
    while (i < display_name.size() && display_name[i] == ' ')
        ++i;
    if (i >= display_name.size())
        return "?";
    std::string s(1, static_cast<char>(display_name[i]));
    if (display_name[i] < 128)
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace ui
