#include "discord/gateway.hpp"

namespace discord {

Gateway::Gateway(std::string token, std::string gateway_url)
    : token_(std::move(token)), gateway_url_(std::move(gateway_url)) {}

Gateway::~Gateway() {
    stop();
}

void Gateway::on_message(MessageHandler h) {
    std::lock_guard<std::mutex> lock(handler_mu_);
    on_message_ = std::move(h);
}

void Gateway::on_ready(ReadyHandler h) {
    std::lock_guard<std::mutex> lock(handler_mu_);
    on_ready_ = std::move(h);
}

bool Gateway::start() {
    running_ = true;
    ReadyHandler rh;
    {
        std::lock_guard<std::mutex> lock(handler_mu_);
        rh = on_ready_;
    }
    if (rh)
        rh({});
    return true;
}

void Gateway::stop() {
    running_ = false;
}

} // namespace discord
