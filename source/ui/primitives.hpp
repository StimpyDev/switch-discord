#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <string>

namespace ui {

void set_color(SDL_Renderer* r, SDL_Color c);
void fill_rect(SDL_Renderer* r, SDL_Rect rect, SDL_Color c);
void fill_hline(SDL_Renderer* r, int x, int y, int w, SDL_Color c);
void fill_circle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c);
void fill_rounded(SDL_Renderer* r, SDL_Rect rect, int radius, SDL_Color c);

SDL_Color avatar_color(const std::string& seed);

void draw_text(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y,
               SDL_Color color, int wrap_w = 0);

std::string avatar_initial(const std::string& display_name);

} // namespace ui
