#pragma once

#include "discord/types.hpp"

#include <curl/curl.h>
#include <jansson.h>

#include <string>
#include <vector>

namespace discord {

class Api {
public:
    explicit Api(std::string bearer_token);
    ~Api();

    Api(const Api&) = delete;
    Api& operator=(const Api&) = delete;
    Api(Api&& other) noexcept;
    Api& operator=(Api&& other) noexcept;

    bool get_gateway_url(std::string& url, std::string& error);
    bool get_channel_messages(const std::string& channel_id, int limit,
                              std::vector<Message>& out, std::string& error);
    bool send_message(const std::string& channel_id, const std::string& content,
                      Message& out, std::string& error);
    bool get_guild_channels(const std::string& guild_id, std::vector<Channel>& out,
                            std::string& error);
    bool get_user_guilds(std::vector<Guild>& out, std::string& error);
    bool get_dm_channels(std::vector<Channel>& out, std::string& error);
    bool get_relationships(std::vector<FriendEntry>& out, std::string& error);
    bool open_dm_channel(const std::string& user_id, Channel& out, std::string& error);

private:
    std::string token_;
    CURL* curl_ = nullptr;
    struct curl_slist* headers_ = nullptr;

    void rebuild_headers();
    bool request(const std::string& method, const std::string& path,
                 const std::string& body, long& http_code, std::string& response,
                 std::string& error);
};

User parse_user(json_t* obj);
Message parse_message(json_t* obj);
Channel parse_channel(json_t* obj);

} // namespace discord
