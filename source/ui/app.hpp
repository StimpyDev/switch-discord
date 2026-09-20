#pragma once

#include "auth/session.hpp"
#include "config.hpp"
#include "discord/api.hpp"
#include "discord/gateway.hpp"
#include "discord/types.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class SidebarTab {
    DirectMessages,
    Friends,
    Guild,
};

class DiscordApp {
public:
    DiscordApp();
    ~DiscordApp();

    bool init(std::string& error);
    void run();
    void shutdown();

private:
    bool load_font(std::string& error);
    bool resolve_user_session(std::string& error);
    bool run_oauth_ui(OAuthPending& pending, std::string& error);
    void refresh_messages();
    void append_message(discord::Message msg);
    void draw();
    void draw_login(const OAuthPending& pending, const std::string& hint);
    void handle_input(const SDL_Event& ev);
    bool open_compose_keyboard(std::string& out);
    void select_guild(size_t index);
    void select_channel(size_t index);
    void select_friend(size_t index);
    void load_guilds();
    void load_dm_channels();
    void load_friends();
    void poll_messages();
    void set_tab(SidebarTab tab);
    void clear_chat_view();
    bool refresh_api_token();

    AppConfig config_;
    UserSession user_session_;
    discord::Api api_;
    std::unique_ptr<discord::Gateway> gateway_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;
    TTF_Font* font_small_ = nullptr;

    SidebarTab tab_ = SidebarTab::Guild;
    std::vector<discord::ReadyGuild> guilds_;
    std::vector<discord::Channel> channels_;
    std::vector<discord::FriendEntry> friends_;
    std::deque<discord::Message> messages_;

    size_t selected_guild_ = 0;
    size_t selected_channel_ = 0;
    size_t selected_friend_ = 0;
    std::string active_channel_id_;
    std::string status_line_;

    std::mutex msg_mu_;
    bool dirty_ = true;
    bool shutdown_done_ = false;
    int scroll_offset_ = 0;
    Uint32 last_poll_ms_ = 0;
    Uint32 last_auth_check_ms_ = 0;
    std::string last_seen_message_id_;
    SDL_GameController* controller_ = nullptr;
};
