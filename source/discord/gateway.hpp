#pragma once

#include "discord/types.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace discord {

struct ReadyGuild {
    std::string id;
    std::string name;
    std::string icon;
};

class Gateway {
public:
    using MessageHandler = std::function<void(const Message&)>;
    using ReadyHandler = std::function<void(const std::vector<ReadyGuild>&)>;

    Gateway(std::string token, std::string gateway_url);
    ~Gateway();

    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    bool start();
    void stop();

    void on_message(MessageHandler h);
    void on_ready(ReadyHandler h);

private:
    std::string token_;
    std::string gateway_url_;
    bool running_ = false;

    std::mutex handler_mu_;
    MessageHandler on_message_;
    ReadyHandler on_ready_;
};

} // namespace discord
