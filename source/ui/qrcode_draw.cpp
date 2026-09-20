#include "ui/qrcode_draw.hpp"

#include "qrcodegen.hpp"

namespace ui {

bool qr_encode(const std::string& text, QrBitmap& out) {
    out = {};
    if (text.empty())
        return false;
    try {
        qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::LOW);
        const int n = qr.getSize();
        out.modules = n;
        out.dark.resize(static_cast<size_t>(n * n));
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                out.dark[static_cast<size_t>(y * n + x)] =
                    qr.getModule(x, y) ? 1 : 0;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

void qr_draw(SDL_Renderer* renderer, int x, int y, int pixel_size, const QrBitmap& qr,
             SDL_Color dark, SDL_Color light, int quiet_modules) {
    if (!renderer || qr.modules <= 0 || pixel_size <= 0)
        return;

    const int n = qr.modules;
    const int border = quiet_modules * pixel_size;
    const int inner = n * pixel_size;
    const int total = inner + border * 2;

    SDL_Rect bg{x - border, y - border, total, total};
    SDL_SetRenderDrawColor(renderer, light.r, light.g, light.b, light.a);
    SDL_RenderFillRect(renderer, &bg);

    SDL_SetRenderDrawColor(renderer, dark.r, dark.g, dark.b, dark.a);
    for (int my = 0; my < n; ++my) {
        for (int mx = 0; mx < n; ++mx) {
            if (!qr.dark[static_cast<size_t>(my * n + mx)])
                continue;
            SDL_Rect cell{x + mx * pixel_size, y + my * pixel_size, pixel_size, pixel_size};
            SDL_RenderFillRect(renderer, &cell);
        }
    }
}

} // namespace ui
