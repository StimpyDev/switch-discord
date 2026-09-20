#include "discord/api.hpp"

#include "version.hpp"

#include <curl/curl.h>
#include <switch.h>

#include <cstring>
#include <sstream>

namespace discord {

namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string field_as_string(json_t* obj, const char* key) {
    json_t* v = json_object_get(obj, key);
    if (!v || !json_is_string(v))
        return {};
    return json_string_value(v);
}

int field_as_int(json_t* obj, const char* key) {
    json_t* v = json_object_get(obj, key);
    if (!v || !json_is_integer(v))
        return 0;
    return static_cast<int>(json_integer_value(v));
}

std::string display_name_for_user(const User& u) {
    if (!u.global_name.empty())
        return u.global_name;
    return u.username;
}

} // namespace

User parse_user(json_t* obj) {
    User u;
    if (!obj)
        return u;
    u.id = field_as_string(obj, "id");
    u.username = field_as_string(obj, "username");
    u.global_name = field_as_string(obj, "global_name");
    if (u.global_name.empty())
        u.global_name = u.username;
    u.avatar = field_as_string(obj, "avatar");
    return u;
}

Message parse_message(json_t* obj) {
    Message m;
    if (!obj)
        return m;
    m.id = field_as_string(obj, "id");
    m.channel_id = field_as_string(obj, "channel_id");
    m.content = field_as_string(obj, "content");
    m.timestamp = field_as_string(obj, "timestamp");
    m.author = parse_user(json_object_get(obj, "author"));
    return m;
}

Channel parse_channel(json_t* obj) {
    Channel c;
    if (!obj)
        return c;
    c.id = field_as_string(obj, "id");
    c.name = field_as_string(obj, "name");
    c.type = field_as_int(obj, "type");
    c.parent_id = field_as_string(obj, "parent_id");
    c.position = field_as_int(obj, "position");
    return c;
}

Api::Api(std::string token) : token_(std::move(token)) {
    curl_ = curl_easy_init();
    if (!curl_)
        return;
    rebuild_headers();
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 15L);
}

void Api::rebuild_headers() {
    if (headers_)
        curl_slist_free_all(headers_);
    headers_ = curl_slist_append(nullptr, "Content-Type: application/json");
    headers_ = curl_slist_append(headers_, ("Authorization: Bearer " + token_).c_str());
    std::string ua = std::string("SwitchDiscord/") + SWITCHDISCORD_VERSION + " (libnx)";
    headers_ = curl_slist_append(headers_, ("User-Agent: " + ua).c_str());
    if (curl_)
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
}

Api::~Api() {
    if (headers_)
        curl_slist_free_all(headers_);
    if (curl_)
        curl_easy_cleanup(curl_);
}

Api::Api(Api&& other) noexcept
    : token_(std::move(other.token_)), curl_(other.curl_),
      headers_(other.headers_) {
    other.curl_ = nullptr;
    other.headers_ = nullptr;
}

Api& Api::operator=(Api&& other) noexcept {
    if (this == &other)
        return *this;
    if (headers_)
        curl_slist_free_all(headers_);
    if (curl_)
        curl_easy_cleanup(curl_);
    token_ = std::move(other.token_);
    curl_ = other.curl_;
    headers_ = other.headers_;
    other.curl_ = nullptr;
    other.headers_ = nullptr;
    return *this;
}

bool Api::request(const std::string& method, const std::string& path,
                  const std::string& body, long& http_code, std::string& response,
                  std::string& error) {
    if (!curl_) {
        error = "curl not initialized";
        return false;
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string url = "https://discord.com/api/v10" + path;
        response.clear();
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, method == "GET" ? 1L : 0L);
        if (!body.empty())
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        else
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, nullptr);

        CURLcode rc = curl_easy_perform(curl_);
        if (rc != CURLE_OK) {
            error = curl_easy_strerror(rc);
            return false;
        }

        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 429 && attempt == 0) {
            svcSleepThread(1'500'000'000LL);
            continue;
        }
        break;
    }

    if (http_code >= 400) {
        if (response.size() > 180)
            response = response.substr(0, 177) + "...";
        error = "HTTP " + std::to_string(http_code) + ": " + response;
        return false;
    }
    return true;
}

bool Api::get_gateway_url(std::string& url, std::string& error) {
    long code = 0;
    std::string body;
    if (!request("GET", "/gateway", "", code, body, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(body.c_str(), 0, &jerr);
    if (!root) {
        error = jerr.text;
        return false;
    }
    url = field_as_string(root, "url");
    json_decref(root);
    if (url.empty()) {
        error = "gateway url missing";
        return false;
    }
    return true;
}

bool Api::get_channel_messages(const std::string& channel_id, int limit,
                               std::vector<Message>& out, std::string& error) {
    long code = 0;
    std::string body;
    std::string path = "/channels/" + channel_id + "/messages?limit=" + std::to_string(limit);
    if (!request("GET", path, "", code, body, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(body.c_str(), 0, &jerr);
    if (!root || !json_is_array(root)) {
        error = "invalid messages response";
        if (root)
            json_decref(root);
        return false;
    }

    size_t n = json_array_size(root);
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
        out.push_back(parse_message(json_array_get(root, i)));

    json_decref(root);
    return true;
}

bool Api::send_message(const std::string& channel_id, const std::string& content,
                       Message& out, std::string& error) {
    json_t* payload = json_object();
    json_object_set_new(payload, "content", json_stringn(content.c_str(), content.size()));
    char* dumped = json_dumps(payload, 0);
    json_decref(payload);
    if (!dumped) {
        error = "json encode failed";
        return false;
    }
    std::string post_body(dumped);
    free(dumped);

    long code = 0;
    std::string resp;
    std::string path = "/channels/" + channel_id + "/messages";
    if (!request("POST", path, post_body, code, resp, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(resp.c_str(), 0, &jerr);
    if (!root) {
        error = jerr.text;
        return false;
    }
    out = parse_message(root);
    json_decref(root);
    return true;
}

bool Api::get_user_guilds(std::vector<Guild>& out, std::string& error) {
    long code = 0;
    std::string body;
    if (!request("GET", "/users/@me/guilds", "", code, body, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(body.c_str(), 0, &jerr);
    if (!root || !json_is_array(root)) {
        error = "invalid guilds response";
        if (root)
            json_decref(root);
        return false;
    }

    out.clear();
    size_t n = json_array_size(root);
    for (size_t i = 0; i < n; ++i) {
        json_t* g = json_array_get(root, i);
        Guild guild;
        guild.id = field_as_string(g, "id");
        guild.name = field_as_string(g, "name");
        out.push_back(std::move(guild));
    }
    json_decref(root);
    return true;
}

bool Api::get_dm_channels(std::vector<Channel>& out, std::string& error) {
    long code = 0;
    std::string body;
    if (!request("GET", "/users/@me/channels", "", code, body, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(body.c_str(), 0, &jerr);
    if (!root || !json_is_array(root)) {
        error = "invalid dm channels response";
        if (root)
            json_decref(root);
        return false;
    }

    out.clear();
    size_t n = json_array_size(root);
    for (size_t i = 0; i < n; ++i) {
        json_t* ch = json_array_get(root, i);
        Channel c = parse_channel(ch);
        if (c.type != 1 && c.type != 3)
            continue;
        if (c.name.empty()) {
            json_t* recipients = json_object_get(ch, "recipients");
            if (recipients && json_is_array(recipients) && json_array_size(recipients) > 0) {
                User u = parse_user(json_array_get(recipients, 0));
                c.dm_label = display_name_for_user(u);
                c.name = c.dm_label;
            }
        } else {
            c.dm_label = c.name;
        }
        if (c.name.empty())
            c.name = "DM";
        out.push_back(std::move(c));
    }
    json_decref(root);
    return true;
}

bool Api::get_relationships(std::vector<FriendEntry>& out, std::string& error) {
    long code = 0;
    std::string body;
    if (!request("GET", "/users/@me/relationships", "", code, body, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(body.c_str(), 0, &jerr);
    if (!root || !json_is_array(root)) {
        error = "invalid relationships response";
        if (root)
            json_decref(root);
        return false;
    }

    out.clear();
    size_t n = json_array_size(root);
    for (size_t i = 0; i < n; ++i) {
        json_t* rel = json_array_get(root, i);
        int type = field_as_int(rel, "type");
        if (type != 1)
            continue;
        json_t* user = json_object_get(rel, "user");
        if (!user)
            continue;
        User u = parse_user(user);
        FriendEntry fe;
        fe.type = type;
        fe.user_id = u.id;
        fe.username = u.username;
        fe.display_name = display_name_for_user(u);
        out.push_back(std::move(fe));
    }
    json_decref(root);
    return true;
}

bool Api::open_dm_channel(const std::string& user_id, Channel& out, std::string& error) {
    json_t* payload = json_object();
    json_object_set_new(payload, "recipient_id", json_string(user_id.c_str()));
    char* dumped = json_dumps(payload, 0);
    json_decref(payload);
    if (!dumped) {
        error = "json encode failed";
        return false;
    }
    std::string post_body(dumped);
    free(dumped);

    long code = 0;
    std::string resp;
    if (!request("POST", "/users/@me/channels", post_body, code, resp, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(resp.c_str(), 0, &jerr);
    if (!root) {
        error = jerr.text;
        return false;
    }
    out = parse_channel(root);
    out.dm_label = out.name;
    json_decref(root);
    return true;
}

bool Api::get_guild_channels(const std::string& guild_id, std::vector<Channel>& out,
                             std::string& error) {
    long code = 0;
    std::string body;
    std::string path = "/guilds/" + guild_id + "/channels";
    if (!request("GET", path, "", code, body, error))
        return false;

    json_error_t jerr{};
    json_t* root = json_loads(body.c_str(), 0, &jerr);
    if (!root || !json_is_array(root)) {
        error = "invalid channels response";
        if (root)
            json_decref(root);
        return false;
    }

    out.clear();
    size_t n = json_array_size(root);
    for (size_t i = 0; i < n; ++i) {
        Channel c = parse_channel(json_array_get(root, i));
        if (c.type == 0 || c.type == 5)
            out.push_back(std::move(c));
    }
    json_decref(root);
    return true;
}

} // namespace discord
