#include "ui/app.hpp"

#include "ui/qrcode_draw.hpp"
#include "ui/theme.hpp"

#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace {

uint64_t snowflake_id(const std::string& id) {
    if (id.empty())
        return 0;
    try {
        return std::stoull(id);
    } catch (...) {
        return 0;
    }
}

} // namespace

namespace {

constexpr int kServerRail = 72;
constexpr int kChannelPanel = 240;
constexpr int kStatusBar = 28;
constexpr int kComposeBar = 44;
constexpr size_t kMaxMessages = 120;

void set_color(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void fill_rect(SDL_Renderer* r, SDL_Rect rect, SDL_Color c) {
    set_color(r, c);
    SDL_RenderFillRect(r, &rect);
}

void draw_text(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y,
               SDL_Color color) {
    if (!font || text.empty())
        return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, 900);
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

std::string display_name(const discord::User& u) {
    if (!u.global_name.empty())
        return u.global_name;
    return u.username;
}

std::string truncate_line(const std::string& s, size_t max_len) {
    if (s.size() <= max_len)
        return s;
    return s.substr(0, max_len - 3) + "...";
}

} // namespace

DiscordApp::DiscordApp() : api_("") {}

DiscordApp::~DiscordApp() {
    shutdown();
}

bool DiscordApp::load_font(std::string& error) {
    const char* paths[] = {
        "sdmc:/switch/switchdiscord/DejaVuSans.ttf",
        "sdmc:/switch/switchdiscord/font.ttf",
    };
    for (const char* path : paths) {
        font_ = TTF_OpenFont(path, 22);
        font_small_ = TTF_OpenFont(path, 16);
        if (font_ && font_small_)
            return true;
        if (font_) {
            TTF_CloseFont(font_);
            font_ = nullptr;
        }
        if (font_small_) {
            TTF_CloseFont(font_small_);
            font_small_ = nullptr;
        }
    }
    error = "Place DejaVuSans.ttf at sdmc:/switch/switchdiscord/DejaVuSans.ttf";
    return false;
}

bool DiscordApp::init(std::string& error) {
    if (!load_config(config_, error))
        return false;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        error = SDL_GetError();
        return false;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    if (SDL_NumJoysticks() > 0)
        controller_ = SDL_GameControllerOpen(0);
    if (TTF_Init() < 0) {
        error = TTF_GetError();
        return false;
    }

    if (!load_font(error))
        return false;

    window_ = SDL_CreateWindow("SwitchDiscord", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1280, 720, SDL_WINDOW_SHOWN);
    if (!window_) {
        error = SDL_GetError();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        error = SDL_GetError();
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    if (!resolve_user_session(error))
        return false;

    const std::string& access_token = user_session_.access_token;
    api_ = discord::Api(access_token);

    std::string gw_url;
    if (!api_.get_gateway_url(gw_url, error))
        return false;

    gateway_ = std::make_unique<discord::Gateway>(access_token, gw_url);
    gateway_->start();

    set_tab(SidebarTab::DirectMessages);
    load_dm_channels();
    load_friends();
    load_guilds();

    if (!config_.guild_id.empty()) {
        tab_ = SidebarTab::Guild;
        for (size_t i = 0; i < guilds_.size(); ++i) {
            if (guilds_[i].id == config_.guild_id) {
                selected_guild_ = i;
                select_guild(i);
                break;
            }
        }
    }

    if (!config_.channel_id.empty()) {
        for (size_t i = 0; i < channels_.size(); ++i) {
            if (channels_[i].id == config_.channel_id) {
                select_channel(i);
                break;
            }
        }
    }

    last_poll_ms_ = SDL_GetTicks();
    last_auth_check_ms_ = last_poll_ms_;
    dirty_ = true;
    return true;
}

void DiscordApp::clear_chat_view() {
    {
        std::lock_guard<std::mutex> lock(msg_mu_);
        messages_.clear();
    }
    active_channel_id_.clear();
    last_seen_message_id_.clear();
    scroll_offset_ = 0;
}

bool DiscordApp::refresh_api_token() {
    std::string err;
    std::string before = user_session_.access_token;
    if (!ensure_user_access_token(config_, user_session_, err)) {
        status_line_ = err;
        dirty_ = true;
        return false;
    }
    if (user_session_.access_token != before) {
        api_ = discord::Api(user_session_.access_token);
    }
    return true;
}

bool DiscordApp::resolve_user_session(std::string& error) {
    load_user_session(user_session_);
    if (ensure_user_access_token(config_, user_session_, error))
        return true;

    user_session_ = {};
    OAuthPending pending{};
    if (!oauth_begin(config_, pending, error))
        return false;
    if (!run_oauth_ui(pending, error))
        return false;
    return !user_session_.access_token.empty();
}

bool DiscordApp::run_oauth_ui(OAuthPending& pending, std::string& error) {
    Uint32 start = SDL_GetTicks();
    bool waiting = true;
    std::string code;

    while (waiting) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                oauth_cancel(pending);
                error = "Login cancelled";
                return false;
            }
            if (ev.type == SDL_JOYBUTTONDOWN && ev.jbutton.button == 10) {
                oauth_cancel(pending);
                error = "Login cancelled";
                return false;
            }
        }

        if (oauth_wait_code(pending, code, error)) {
            waiting = false;
            break;
        }

        draw_login(pending, "Scan the QR code with your phone. Waiting for login...");
        SDL_Delay(16);

        if (SDL_GetTicks() - start > 300000) {
            oauth_cancel(pending);
            error = "OAuth timeout (5 min)";
            return false;
        }
    }

    if (!oauth_exchange_code(config_, code, pending.code_verifier, user_session_, error))
        return false;
    return true;
}

void DiscordApp::draw_login(const OAuthPending& pending, const std::string& hint) {
    static std::string cached_url;
    static ui::QrBitmap cached_qr;

    if (pending.authorize_url != cached_url) {
        cached_url = pending.authorize_url;
        ui::qr_encode(cached_url, cached_qr);
    }

    fill_rect(renderer_, {0, 0, 1280, 720}, theme::bg());
    draw_text(renderer_, font_, "Discord login", 40, 40, theme::text_primary());
    draw_text(renderer_, font_small_, hint, 40, 88, theme::text_muted());
    draw_text(renderer_, font_small_, "Phone and Switch must be on the same Wi-Fi.",
              40, 118, theme::text_muted());

    if (cached_qr.modules > 0) {
        const int max_px = 400;
        int pixel = max_px / (cached_qr.modules + 8);
        if (pixel < 4)
            pixel = 4;
        const int drawn = (cached_qr.modules + 8) * pixel;
        const int qx = (1280 - drawn) / 2 + 4 * pixel;
        const int qy = 160;
        ui::qr_draw(renderer_, qx, qy, pixel, cached_qr, theme::text_primary(),
                    {255, 255, 255, 255});
    } else {
        draw_text(renderer_, font_small_, "Could not build QR (URL too long?)",
                  40, 200, theme::accent());
        draw_text(renderer_, font_small_, truncate_line(pending.authorize_url, 120), 40, 240,
                  theme::text_muted());
    }

    draw_text(renderer_, font_small_,
              "If scan fails: open the OAuth redirect from Discord on your phone.",
              40, 580, theme::text_muted());
    draw_text(renderer_, font_small_, "+ : cancel", 40, 660, theme::text_muted());
    SDL_RenderPresent(renderer_);
}

void DiscordApp::set_tab(SidebarTab tab) {
    if (tab_ == tab)
        return;
    tab_ = tab;
    clear_chat_view();
    channels_.clear();
    if (tab_ == SidebarTab::DirectMessages)
        load_dm_channels();
    else if (tab_ == SidebarTab::Friends) {
        load_friends();
        status_line_ = "Friends — A to open DM";
    } else if (tab_ == SidebarTab::Guild && !guilds_.empty())
        select_guild(selected_guild_);
    else if (tab_ == SidebarTab::Guild)
        status_line_ = "No servers";
    dirty_ = true;
}

void DiscordApp::load_guilds() {
    std::vector<discord::Guild> fetched;
    std::string err;
    if (!api_.get_user_guilds(fetched, err)) {
        status_line_ = err;
        return;
    }
    guilds_.clear();
    guilds_.reserve(fetched.size());
    for (const auto& g : fetched) {
        discord::ReadyGuild rg;
        rg.id = g.id;
        rg.name = g.name;
        guilds_.push_back(std::move(rg));
    }
    status_line_ = "Loaded " + std::to_string(guilds_.size()) + " server(s)";
    dirty_ = true;
}

void DiscordApp::load_dm_channels() {
    std::string err;
    if (!api_.get_dm_channels(channels_, err)) {
        status_line_ = err;
        return;
    }
    if (channels_.empty()) {
        clear_chat_view();
        status_line_ = "No direct messages";
    } else {
        selected_channel_ = 0;
        select_channel(0);
        status_line_ = "Direct messages";
    }
    dirty_ = true;
}

void DiscordApp::load_friends() {
    std::string err;
    if (!api_.get_relationships(friends_, err)) {
        status_line_ = err;
        return;
    }
    dirty_ = true;
}

void DiscordApp::select_friend(size_t index) {
    if (friends_.empty())
        return;
    selected_friend_ = std::min(index, friends_.size() - 1);
    discord::Channel dm;
    std::string err;
    if (!api_.open_dm_channel(friends_[selected_friend_].user_id, dm, err)) {
        status_line_ = err;
        return;
    }
    active_channel_id_ = dm.id;
    scroll_offset_ = 0;
    refresh_messages();
    status_line_ = "@" + friends_[selected_friend_].display_name;
    dirty_ = true;
}

void DiscordApp::poll_messages() {
    if (active_channel_id_.empty())
        return;
    Uint32 now = SDL_GetTicks();
    if (now - last_poll_ms_ < 3000)
        return;
    last_poll_ms_ = now;

    std::vector<discord::Message> latest;
    std::string err;
    if (!api_.get_channel_messages(active_channel_id_, 10, latest, err))
        return;

    if (latest.empty())
        return;

    const uint64_t last_seen = snowflake_id(last_seen_message_id_);
    std::vector<discord::Message> incoming;
    incoming.reserve(latest.size());
    for (auto& m : latest) {
        if (snowflake_id(m.id) > last_seen)
            incoming.push_back(std::move(m));
    }
    if (incoming.empty())
        return;

    std::sort(incoming.begin(), incoming.end(), [](const discord::Message& a, const discord::Message& b) {
        return snowflake_id(a.id) < snowflake_id(b.id);
    });

    for (auto& m : incoming)
        append_message(std::move(m));
}

void DiscordApp::shutdown() {
    if (shutdown_done_)
        return;
    shutdown_done_ = true;
    if (gateway_)
        gateway_->stop();
    if (controller_) {
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
    }
    if (font_small_) {
        TTF_CloseFont(font_small_);
        font_small_ = nullptr;
    }
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    TTF_Quit();
    SDL_Quit();
}

void DiscordApp::select_guild(size_t index) {
    if (guilds_.empty())
        return;
    tab_ = SidebarTab::Guild;
    selected_guild_ = std::min(index, guilds_.size() - 1);
    std::string err;
    channels_.clear();
    if (!api_.get_guild_channels(guilds_[selected_guild_].id, channels_, err)) {
        status_line_ = err;
        return;
    }
    std::sort(channels_.begin(), channels_.end(),
              [](const discord::Channel& a, const discord::Channel& b) {
                  return a.position < b.position;
              });
    selected_channel_ = 0;
    if (!channels_.empty())
        select_channel(0);
    dirty_ = true;
}

void DiscordApp::select_channel(size_t index) {
    if (channels_.empty())
        return;
    selected_channel_ = std::min(index, channels_.size() - 1);
    active_channel_id_ = channels_[selected_channel_].id;
    scroll_offset_ = 0;
    refresh_messages();
    dirty_ = true;
}

void DiscordApp::refresh_messages() {
    if (active_channel_id_.empty())
        return;
    std::string err;
    std::vector<discord::Message> fetched;
    if (!api_.get_channel_messages(active_channel_id_, 50, fetched, err)) {
        status_line_ = err;
        return;
    }
    std::reverse(fetched.begin(), fetched.end());
    {
        std::lock_guard<std::mutex> lock(msg_mu_);
        messages_.clear();
        for (auto& m : fetched)
            messages_.push_back(std::move(m));
        if (!messages_.empty())
            last_seen_message_id_ = messages_.back().id;
    }
    if (selected_channel_ < channels_.size()) {
        if (tab_ == SidebarTab::DirectMessages)
            status_line_ = "@" + channels_[selected_channel_].name;
        else
            status_line_ = "#" + channels_[selected_channel_].name;
    }
    dirty_ = true;
}

void DiscordApp::append_message(discord::Message msg) {
    {
        std::lock_guard<std::mutex> lock(msg_mu_);
        for (const auto& existing : messages_) {
            if (existing.id == msg.id)
                return;
        }
        if (snowflake_id(msg.id) > snowflake_id(last_seen_message_id_))
            last_seen_message_id_ = msg.id;
        messages_.push_back(std::move(msg));
        while (messages_.size() > kMaxMessages)
            messages_.pop_front();
    }
    dirty_ = true;
}

bool DiscordApp::open_compose_keyboard(std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0)))
        return false;

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, "Message");
    swkbdConfigSetInitialText(&kbd, "");
    swkbdConfigSetStringLenMax(&kbd, 2000);

    char buf[2001] = {};
    Result rc = swkbdShow(&kbd, buf, sizeof(buf) - 1);
    swkbdClose(&kbd);
    if (R_FAILED(rc) || buf[0] == '\0')
        return false;
    out = buf;
    return true;
}

void DiscordApp::handle_input(const SDL_Event& ev) {
    if (ev.type == SDL_JOYBUTTONDOWN) {
        switch (ev.jbutton.button) {
        case 0: { // A
            if (tab_ == SidebarTab::Friends && !friends_.empty() && active_channel_id_.empty()) {
                select_friend(selected_friend_);
                break;
            }
            std::string text;
            if (open_compose_keyboard(text) && !active_channel_id_.empty()) {
                discord::Message sent;
                std::string err;
                if (api_.send_message(active_channel_id_, text, sent, err))
                    append_message(sent);
                else
                    status_line_ = err;
            }
            dirty_ = true;
            break;
        }
        case 1: // B
            break;
        case 10: { // Plus — exit
            SDL_Event quit{};
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
            break;
        }
        case 4: // L
            if (tab_ == SidebarTab::Friends && !friends_.empty() && selected_friend_ > 0)
                select_friend(selected_friend_ - 1);
            else if (!channels_.empty() && selected_channel_ > 0)
                select_channel(selected_channel_ - 1);
            break;
        case 5: // R
            if (tab_ == SidebarTab::Friends && !friends_.empty() &&
                selected_friend_ + 1 < friends_.size())
                select_friend(selected_friend_ + 1);
            else if (!channels_.empty() && selected_channel_ + 1 < channels_.size())
                select_channel(selected_channel_ + 1);
            break;
        case 2: // X — prev server / select friend
            if (tab_ == SidebarTab::Guild && !guilds_.empty() && selected_guild_ > 0)
                select_guild(selected_guild_ - 1);
            break;
        case 3: // Y — tab / next
            if (tab_ == SidebarTab::Guild && !guilds_.empty() &&
                selected_guild_ + 1 < guilds_.size())
                select_guild(selected_guild_ + 1);
            break;
        case 9: // Minus — cycle DM / Friends / Guild
            if (tab_ == SidebarTab::DirectMessages)
                set_tab(SidebarTab::Friends);
            else if (tab_ == SidebarTab::Friends)
                set_tab(SidebarTab::Guild);
            else
                set_tab(SidebarTab::DirectMessages);
            break;
        default:
            break;
        }
    } else if (ev.type == SDL_JOYHATMOTION) {
        if (ev.jhat.value & SDL_HAT_UP)
            scroll_offset_ = std::max(0, scroll_offset_ - 1);
        if (ev.jhat.value & SDL_HAT_DOWN)
            scroll_offset_ += 1;
        int msg_count = 0;
        {
            std::lock_guard<std::mutex> lock(msg_mu_);
            msg_count = static_cast<int>(messages_.size());
        }
        const int max_scroll = std::max(0, msg_count - 8);
        scroll_offset_ = std::min(scroll_offset_, max_scroll);
        dirty_ = true;
    }

}

void DiscordApp::draw() {
    fill_rect(renderer_, {0, 0, 1280, 720}, theme::bg());

    fill_rect(renderer_, {0, 0, kServerRail, 720}, theme::sidebar());
    fill_rect(renderer_, {kServerRail, 0, kChannelPanel, 720}, theme::panel());

    int chat_x = kServerRail + kChannelPanel;
    int chat_w = 1280 - chat_x;
    fill_rect(renderer_, {chat_x, 0, chat_w, 720 - kComposeBar - kStatusBar}, theme::chat_bg());
    fill_rect(renderer_, {chat_x, 720 - kComposeBar - kStatusBar, chat_w, kStatusBar}, theme::panel());
    fill_rect(renderer_, {chat_x, 720 - kComposeBar, chat_w, kComposeBar}, theme::sidebar());

    int sy = 16;
    auto draw_rail_btn = [&](const char* label, bool active) {
        SDL_Rect icon{kServerRail / 2 - 24, sy, 48, 48};
        fill_rect(renderer_, icon, active ? theme::accent() : theme::panel());
        draw_text(renderer_, font_small_, label, icon.x + 16, icon.y + 14, theme::text_primary());
        sy += 56;
    };
    draw_rail_btn("@", tab_ == SidebarTab::DirectMessages);
    draw_rail_btn("F", tab_ == SidebarTab::Friends);
    for (size_t i = 0; i < guilds_.size() && sy < 700; ++i) {
        SDL_Rect icon{kServerRail / 2 - 24, sy, 48, 48};
        bool active = tab_ == SidebarTab::Guild && i == selected_guild_;
        fill_rect(renderer_, icon, active ? theme::accent() : theme::panel());
        std::string label = guilds_[i].name.empty() ? "?" : std::string(1, guilds_[i].name[0]);
        draw_text(renderer_, font_small_, label, icon.x + 16, icon.y + 14, theme::text_primary());
        sy += 56;
    }

    int cy = 16;
    if (tab_ == SidebarTab::Guild && !guilds_.empty())
        draw_text(renderer_, font_, guilds_[selected_guild_].name, kServerRail + 16, cy,
                  theme::text_primary());
    else if (tab_ == SidebarTab::DirectMessages)
        draw_text(renderer_, font_, "Direct Messages", kServerRail + 16, cy, theme::text_primary());
    else if (tab_ == SidebarTab::Friends)
        draw_text(renderer_, font_, "Friends", kServerRail + 16, cy, theme::text_primary());
    cy += 36;

    if (tab_ == SidebarTab::Friends) {
        for (size_t i = 0; i < friends_.size() && cy < 680; ++i) {
            SDL_Color col = (i == selected_friend_) ? theme::text_primary() : theme::text_muted();
            draw_text(renderer_, font_small_, friends_[i].display_name, kServerRail + 16, cy, col);
            cy += 28;
        }
    } else {
        for (size_t i = 0; i < channels_.size() && cy < 680; ++i) {
            SDL_Color col = (i == selected_channel_) ? theme::text_primary() : theme::text_muted();
            std::string prefix = (tab_ == SidebarTab::DirectMessages) ? "" : "# ";
            draw_text(renderer_, font_small_, prefix + channels_[i].name, kServerRail + 16, cy, col);
            cy += 28;
        }
    }

    // Messages
    int my = 12;
    std::deque<discord::Message> copy;
    {
        std::lock_guard<std::mutex> lock(msg_mu_);
        copy = messages_;
    }
    int visible_start = std::max(0, static_cast<int>(copy.size()) - 12 - scroll_offset_);
    for (int i = visible_start; i < static_cast<int>(copy.size()) && my < 720 - kComposeBar - kStatusBar - 40; ++i) {
        const auto& m = copy[static_cast<size_t>(i)];
        std::string header = display_name(m.author);
        draw_text(renderer_, font_small_, header, chat_x + 16, my, theme::accent());
        my += 20;
        draw_text(renderer_, font_small_, truncate_line(m.content, 500), chat_x + 16, my,
                  theme::text_primary());
        my += 36;
    }

    draw_text(renderer_, font_small_, status_line_, chat_x + 12, 720 - kComposeBar - kStatusBar + 6,
              theme::text_muted());
    draw_text(renderer_, font_small_,
              "-: Tab  A: Send  L/R: List  X/Y: Server  +: Quit", chat_x + 12,
              720 - kComposeBar + 12, theme::text_muted());

    SDL_RenderPresent(renderer_);
}

void DiscordApp::run() {
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = false;
            else
                handle_input(ev);
        }

        if (tab_ == SidebarTab::Guild && !guilds_.empty() && channels_.empty())
            select_guild(selected_guild_);

        Uint32 now = SDL_GetTicks();
        if (now - last_auth_check_ms_ >= 600000) {
            refresh_api_token();
            last_auth_check_ms_ = now;
        }

        poll_messages();

        if (dirty_) {
            draw();
            dirty_ = false;
        } else {
            SDL_Delay(16);
        }
    }
}
