#include "ui/app.hpp"

#include "ui/primitives.hpp"
#include "ui/qrcode_draw.hpp"
#include "ui/theme.hpp"

#include <switch.h>

#include <algorithm>
#include <cctype>
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
constexpr int kHeaderH = 48;
constexpr int kUserBarH = 52;
constexpr int kInputH = 68;
constexpr int kServerIcon = 48;
constexpr size_t kMaxMessages = 120;

bool is_text_channel(const discord::Channel& c) {
    return c.type == 0;
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

std::string message_time(const std::string& ts) {
    if (ts.size() >= 16)
        return ts.substr(11, 5);
    return {};
}

void draw_avatar(SDL_Renderer* r, TTF_Font* font, int cx, int cy, int radius,
                 const std::string& name) {
    ui::fill_circle(r, cx, cy, radius, ui::avatar_color(name));
    ui::draw_text(r, font, ui::avatar_initial(name), cx - radius / 2, cy - radius / 2 + 2,
                  theme::header_primary());
}

void draw_server_rail_icon(SDL_Renderer* r, TTF_Font* font, int y, bool active, bool home,
                           const std::string& letter) {
    const int cx = kServerRail / 2;
    const int cy = y + kServerIcon / 2;
    if (active) {
        ui::fill_rounded(r, {0, cy - 10, 4, 20}, 2, theme::pill());
    }
    if (home) {
        ui::fill_rounded(r, {cx - kServerIcon / 2, y, kServerIcon, kServerIcon}, 24,
                         active ? theme::brand() : theme::brand_hover());
        ui::fill_rounded(r, {cx - 10, y + 14, 20, 20}, 10, theme::header_primary());
    } else {
        ui::fill_rounded(r, {cx - kServerIcon / 2, y, kServerIcon, kServerIcon}, 16,
                         theme::server_icon_bg());
        if (!letter.empty()) {
            ui::draw_text(r, font, letter, cx - 6, y + 14, theme::header_primary());
        }
    }
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
        font_ = TTF_OpenFont(path, 20);
        font_small_ = TTF_OpenFont(path, 16);
        font_tiny_ = TTF_OpenFont(path, 13);
        if (font_ && font_small_ && font_tiny_)
            return true;
        if (font_) {
            TTF_CloseFont(font_);
            font_ = nullptr;
        }
        if (font_small_) {
            TTF_CloseFont(font_small_);
            font_small_ = nullptr;
        }
        if (font_tiny_) {
            TTF_CloseFont(font_tiny_);
            font_tiny_ = nullptr;
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

    {
        std::string me_err;
        have_me_ = api_.get_current_user(me_, me_err);
        if (!have_me_) {
            error = "Discord API: " + me_err;
            return false;
        }
    }

    load_guilds();
    load_friends();
    load_dm_channels();
    set_tab(SidebarTab::DirectMessages);

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

        std::string phone_verifier;
        if (oauth_wait_code(pending, code, phone_verifier, error)) {
            waiting = false;
            if (!phone_verifier.empty())
                pending.code_verifier = phone_verifier;
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

    ui::fill_rect(renderer_, {0, 0, 1280, 720}, theme::bg_primary());
    ui::fill_rounded(renderer_, {340, 80, 600, 560}, 8, theme::bg_secondary());
    ui::fill_hline(renderer_, 340, 128, 600, theme::divider());

    ui::draw_text(renderer_, font_, "Log in to Discord", 368, 96, theme::header_primary());
    ui::draw_text(renderer_, font_small_, hint, 368, 148, theme::text_muted());
    ui::draw_text(renderer_, font_tiny_, "Use the same Wi-Fi on your phone and Switch.", 368, 168,
                  theme::text_muted());
    ui::draw_text(renderer_, font_tiny_,
                  "QR: login page, then Discord. Do not open oauth-relay yourself.", 368, 188,
                  theme::text_muted());
    if (!pending.switch_ip.empty()) {
        ui::draw_text(renderer_, font_small_, "Switch IP: " + pending.switch_ip, 368, 212,
                      theme::header_primary());
    }

    if (cached_qr.modules > 0) {
        const int max_px = 320;
        int pixel = max_px / (cached_qr.modules + 8);
        if (pixel < 4)
            pixel = 4;
        const int drawn = (cached_qr.modules + 8) * pixel;
        const int qx = (1280 - drawn) / 2 + 4 * pixel;
        const int qy = 250;
        ui::qr_draw(renderer_, qx, qy, pixel, cached_qr, {32, 34, 37, 255}, {255, 255, 255, 255});
    } else {
        ui::draw_text(renderer_, font_small_, "QR could not be generated.", 368, 240, theme::brand());
    }

    ui::draw_text(renderer_, font_tiny_, "+ to cancel", 368, 600, theme::text_muted());
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
        status_line_.clear();
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
    if (guilds_.empty())
        status_line_ = "0 servers — delete auth.json and log in again if wrong";
    else
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
    status_line_.clear();
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
    if (font_tiny_) {
        TTF_CloseFont(font_tiny_);
        font_tiny_ = nullptr;
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
    if (!channels_.empty()) {
        for (size_t i = 0; i < channels_.size(); ++i) {
            if (is_text_channel(channels_[i])) {
                select_channel(i);
                break;
            }
        }
    }
    dirty_ = true;
}

void DiscordApp::select_channel(size_t index) {
    if (channels_.empty())
        return;
    index = std::min(index, channels_.size() - 1);
    if (!is_text_channel(channels_[index])) {
        for (size_t i = index; i < channels_.size(); ++i) {
            if (is_text_channel(channels_[i])) {
                index = i;
                break;
            }
        }
    }
    if (!is_text_channel(channels_[index]))
        return;
    selected_channel_ = index;
    active_channel_id_ = channels_[selected_channel_].id;
    scroll_offset_ = 0;
    status_line_.clear();
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
        case 4: { // L
            if (tab_ == SidebarTab::Friends && !friends_.empty() && selected_friend_ > 0)
                select_friend(selected_friend_ - 1);
            else if (!channels_.empty()) {
                size_t i = selected_channel_;
                while (i > 0) {
                    --i;
                    if (is_text_channel(channels_[i])) {
                        select_channel(i);
                        break;
                    }
                }
            }
            break;
        }
        case 5: { // R
            if (tab_ == SidebarTab::Friends && !friends_.empty() &&
                selected_friend_ + 1 < friends_.size())
                select_friend(selected_friend_ + 1);
            else if (!channels_.empty()) {
                for (size_t i = selected_channel_ + 1; i < channels_.size(); ++i) {
                    if (is_text_channel(channels_[i])) {
                        select_channel(i);
                        break;
                    }
                }
            }
            break;
        }
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
    const int chat_x = kServerRail + kChannelPanel;
    const int chat_w = 1280 - chat_x;
    const int list_h = 720 - kUserBarH;

    ui::fill_rect(renderer_, {0, 0, 1280, 720}, theme::bg_primary());
    ui::fill_rect(renderer_, {0, 0, kServerRail, 720}, theme::bg_tertiary());
    ui::fill_rect(renderer_, {kServerRail, 0, kChannelPanel, list_h}, theme::bg_secondary());
    ui::fill_rect(renderer_, {kServerRail, list_h, kChannelPanel, kUserBarH}, theme::bg_floating());
    ui::fill_rect(renderer_, {chat_x, 0, chat_w, kHeaderH}, theme::bg_secondary());
    ui::fill_rect(renderer_, {chat_x, kHeaderH, chat_w, 720 - kHeaderH - kInputH}, theme::bg_primary());

    ui::fill_hline(renderer_, kServerRail, 0, kChannelPanel, theme::divider());
    ui::fill_hline(renderer_, chat_x, kHeaderH, chat_w, theme::divider());
    ui::fill_hline(renderer_, kServerRail, list_h, kChannelPanel, theme::divider());

    int sy = 12;
    draw_server_rail_icon(renderer_, font_small_, sy, tab_ == SidebarTab::DirectMessages, true, {});
    sy += kServerIcon + 8;
    draw_server_rail_icon(renderer_, font_small_, sy, tab_ == SidebarTab::Friends, false, "F");
    sy += kServerIcon + 8;
    ui::fill_hline(renderer_, 16, sy, 40, theme::divider());
    sy += 8;

    for (size_t i = 0; i < guilds_.size() && sy < 680; ++i) {
        bool active = tab_ == SidebarTab::Guild && i == selected_guild_;
        std::string letter = guilds_[i].name.empty() ? "?" : std::string(1, guilds_[i].name[0]);
        if (!letter.empty() && letter[0] >= 'a' && letter[0] <= 'z')
            letter[0] = static_cast<char>(letter[0] - 32);
        draw_server_rail_icon(renderer_, font_small_, sy, active, false, letter);
        sy += kServerIcon + 8;
    }

    const int panel_x = kServerRail;
    std::string panel_title = "Direct Messages";
    if (tab_ == SidebarTab::Guild && !guilds_.empty())
        panel_title = guilds_[selected_guild_].name;
    else if (tab_ == SidebarTab::Friends)
        panel_title = "Friends";

    ui::draw_text(renderer_, font_, truncate_line(panel_title, 22), panel_x + 16, 14,
                  theme::header_primary());
    ui::fill_hline(renderer_, panel_x, kHeaderH - 1, kChannelPanel, theme::divider());

    int cy = kHeaderH + 8;
    const int row_h = 34;

    if (tab_ == SidebarTab::Friends) {
        for (size_t i = 0; i < friends_.size() && cy < list_h - 8; ++i) {
            const bool sel = i == selected_friend_;
            if (sel)
                ui::fill_rounded(renderer_, {panel_x + 8, cy, kChannelPanel - 16, row_h - 2}, 4,
                                 theme::bg_selected());
            draw_avatar(renderer_, font_tiny_, panel_x + 28, cy + row_h / 2, 14,
                        friends_[i].display_name);
            SDL_Color col = sel ? theme::text_normal() : theme::text_muted();
            ui::draw_text(renderer_, font_small_, friends_[i].display_name, panel_x + 48, cy + 8,
                          col);
            cy += row_h;
        }
    } else {
        for (size_t i = 0; i < channels_.size() && cy < list_h - 8; ++i) {
            const auto& ch = channels_[i];
            if (ch.type == 4) {
                std::string cat = ch.name;
                for (char& c : cat)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                ui::draw_text(renderer_, font_tiny_, cat, panel_x + 16, cy + 4, theme::text_muted());
                cy += 26;
                continue;
            }
            if (!is_text_channel(ch))
                continue;

            const bool sel = i == selected_channel_;
            if (sel)
                ui::fill_rounded(renderer_, {panel_x + 8, cy, kChannelPanel - 16, row_h - 2}, 4,
                                 theme::bg_selected());

            int tx = panel_x + 16;
            if (tab_ == SidebarTab::DirectMessages) {
                draw_avatar(renderer_, font_tiny_, panel_x + 28, cy + row_h / 2, 14, ch.name);
                tx = panel_x + 48;
            } else {
                ui::draw_text(renderer_, font_tiny_, "#", tx, cy + 8, theme::text_channel_icon());
                tx += 18;
            }
            SDL_Color col = sel ? theme::text_normal() : theme::text_muted();
            ui::draw_text(renderer_, font_small_, truncate_line(ch.name, 24), tx, cy + 8, col);
            cy += row_h;
        }
    }

    std::string me_name = have_me_ ? display_name(me_) : "User";
    const int me_cy = list_h + kUserBarH / 2;
    draw_avatar(renderer_, font_tiny_, panel_x + 28, me_cy, 16, me_name);
    ui::fill_circle(renderer_, panel_x + 38, me_cy + 10, 6, theme::bg_floating());
    ui::fill_circle(renderer_, panel_x + 38, me_cy + 10, 4, theme::online());
    ui::draw_text(renderer_, font_small_, truncate_line(me_name, 16), panel_x + 50, list_h + 10,
                  theme::header_primary());
    ui::draw_text(renderer_, font_tiny_, "Online", panel_x + 50, list_h + 30, theme::text_muted());

    std::string chat_title;
    bool chat_is_dm = tab_ == SidebarTab::DirectMessages || tab_ == SidebarTab::Friends;
    if (tab_ == SidebarTab::Friends && !friends_.empty() && !active_channel_id_.empty())
        chat_title = friends_[selected_friend_].display_name;
    else if (!channels_.empty() && selected_channel_ < channels_.size() &&
             is_text_channel(channels_[selected_channel_]))
        chat_title = channels_[selected_channel_].name;
    else if (tab_ == SidebarTab::DirectMessages)
        chat_title = "Direct Messages";
    else if (tab_ == SidebarTab::Friends)
        chat_title = "Friends";
    else
        chat_title = panel_title;

    int title_x = chat_x + 16;
    if (!chat_is_dm && tab_ == SidebarTab::Guild) {
        ui::draw_text(renderer_, font_, "#", title_x, 12, theme::text_channel_icon());
        title_x += 22;
    }
    ui::draw_text(renderer_, font_, truncate_line(chat_title, 40), title_x, 12,
                  theme::header_primary());
    if (!status_line_.empty()) {
        ui::draw_text(renderer_, font_tiny_, truncate_line(status_line_, 90), chat_x + 16,
                      kHeaderH - 18, theme::text_muted());
    }

    std::deque<discord::Message> copy;
    {
        std::lock_guard<std::mutex> lock(msg_mu_);
        copy = messages_;
    }

    const int msg_top = kHeaderH + 16;
    const int msg_bottom = 720 - kInputH - 12;
    int my = msg_top;
    int visible_start = std::max(0, static_cast<int>(copy.size()) - 10 - scroll_offset_);
    std::string last_author;
    for (int i = visible_start; i < static_cast<int>(copy.size()) && my < msg_bottom; ++i) {
        const auto& m = copy[static_cast<size_t>(i)];
        const std::string author = display_name(m.author);
        const bool grouped = author == last_author;
        last_author = author;

        if (!grouped) {
            draw_avatar(renderer_, font_tiny_, chat_x + 36, my + 18, 18, author);
            ui::draw_text(renderer_, font_small_, author, chat_x + 68, my, theme::header_primary());
            std::string ts = message_time(m.timestamp);
            if (!ts.empty())
                ui::draw_text(renderer_, font_tiny_, ts, chat_x + 68 + static_cast<int>(author.size()) * 9,
                              my + 2, theme::text_muted());
            my += 26;
        } else {
            my += 4;
        }

        ui::draw_text(renderer_, font_small_, truncate_line(m.content, 500), chat_x + 68, my,
                      theme::text_normal(), chat_w - 84);
        my += grouped ? 22 : 28;
    }

    std::string placeholder = "Message";
    if (!chat_title.empty() && tab_ == SidebarTab::Guild)
        placeholder += " #" + chat_title;
    else if (!chat_title.empty() && chat_is_dm)
        placeholder += " @" + chat_title;
    if (!status_line_.empty())
        placeholder = truncate_line(status_line_, 80);

    ui::fill_rounded(renderer_, {chat_x + 16, 720 - kInputH + 12, chat_w - 32, 44}, 8,
                     theme::bg_input());
    ui::draw_text(renderer_, font_small_, placeholder, chat_x + 28, 720 - kInputH + 24,
                  status_line_.empty() ? theme::text_muted() : theme::text_normal());

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
