#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace discord {

struct User {
    std::string id;
    std::string username;
    std::string global_name;
    std::string avatar;
};

struct Channel {
    std::string id;
    std::string name;
    int type = 0;
    std::string parent_id;
    int position = 0;
    std::string dm_label;
};

struct FriendEntry {
    std::string user_id;
    std::string username;
    std::string display_name;
    int type = 0;
};

struct Guild {
    std::string id;
    std::string name;
    std::string icon;
    std::vector<Channel> channels;
};

struct Message {
    std::string id;
    std::string channel_id;
    User author;
    std::string content;
    std::string timestamp;
};

} // namespace discord
