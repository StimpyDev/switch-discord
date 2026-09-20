#include "auth/session.hpp"

#include "paths.hpp"

#include <curl/curl.h>
#include <jansson.h>
#include <switch.h>
#include <switch/crypto/sha256.h>
#include <switch/services/csrng.h>
#include <switch/services/nifm.h>

#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <random>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <cstdio>
#include <unistd.h>

namespace {

constexpr int kCallbackPort = 8765;

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string base64url_encode(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len)
            n |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < len)
            n |= static_cast<unsigned int>(data[i + 2]);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? tbl[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? tbl[n & 63] : '=');
    }
    for (char& c : out) {
        if (c == '+')
            c = '-';
        else if (c == '/')
            c = '_';
    }
    while (!out.empty() && out.back() == '=')
        out.pop_back();
    return out;
}

std::string random_verifier(size_t len) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
    std::string out;
    out.reserve(len);
    u64 seed = 0;
    csrngGetRandomBytes(&seed, sizeof(seed));
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    for (size_t i = 0; i < len; ++i)
        out.push_back(charset[dist(rng)]);
    return out;
}

std::string url_encode(const std::string& s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            oss << c;
        else
            oss << '%' << std::uppercase << std::hex << (c >> 4) << (c & 15) << std::nouppercase;
    }
    return oss.str();
}

bool get_switch_ipv4(std::string& ip_out, std::string& error) {
    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) {
        error = "nifmInitialize failed";
        return false;
    }
    u32 ip = 0;
    rc = nifmGetCurrentIpAddress(&ip);
    nifmExit();
    if (R_FAILED(rc) || ip == 0) {
        error = "Connect Switch to Wi-Fi first";
        return false;
    }
    struct in_addr addr;
    addr.s_addr = ip;
    ip_out = inet_ntoa(addr);
    return true;
}

std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            int hi = hex(in[i + 1]);
            int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

std::string parse_query_param(const std::string& query, const char* key) {
    std::string needle = std::string(key) + "=";
    size_t pos = query.find(needle);
    if (pos == std::string::npos)
        return {};
    pos += needle.size();
    size_t end = query.find('&', pos);
    if (end == std::string::npos)
        end = query.size();
    return url_decode(query.substr(pos, end - pos));
}

bool accept_oauth_code(int server_fd, std::string& code_out, std::string& code_verifier_out,
                       std::string& error) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(server_fd, &fds);
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    int sel = select(server_fd + 1, &fds, nullptr, nullptr, &tv);
    if (sel <= 0)
        return false;

    sockaddr_in client{};
    socklen_t clen = sizeof(client);
    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client), &clen);
    if (client_fd < 0)
        return false;

    char buf[16384];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return false;
    }
    buf[n] = '\0';

    std::string req(buf);
    size_t line_end = req.find("\r\n");
    std::string line = line_end == std::string::npos ? req : req.substr(0, line_end);
    size_t sp1 = line.find(' ');
    size_t sp2 = line.find(' ', sp1 + 1);
    std::string path = (sp1 == std::string::npos || sp2 == std::string::npos)
                           ? line
                           : line.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t qm = path.find('?');
    std::string query = qm == std::string::npos ? "" : path.substr(qm + 1);
    code_out = parse_query_param(query, "code");
    code_verifier_out = parse_query_param(query, "code_verifier");

    const char* resp =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
        "<html><body><p>OK — back to Switch.</p></body></html>";
    send(client_fd, resp, strlen(resp), 0);
    close(client_fd);

    if (code_out.empty()) {
        error = "No ?code= in callback";
        return false;
    }
    return true;
}

std::string trim_copy(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

bool token_request(const AppConfig& config, const std::string& body, UserSession& session,
                   std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://discord.com/api/v10/oauth2/token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "User-Agent: Switchcord (OAuth)");
    if (!config.client_secret.empty()) {
        std::string basic = config.client_id + ":" + config.client_secret;
        // Discord accepts client_id/client_secret in POST body instead for simplicity
        (void)basic;
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        error = curl_easy_strerror(rc);
        return false;
    }
    if (http >= 400) {
        error = "OAuth token HTTP " + std::to_string(http) + ": " + response;
        return false;
    }

    json_error_t jerr{};
    json_t* root = json_loads(response.c_str(), 0, &jerr);
    if (!root) {
        error = jerr.text;
        return false;
    }

    json_t* access = json_object_get(root, "access_token");
    json_t* refresh = json_object_get(root, "refresh_token");
    json_t* expires = json_object_get(root, "expires_in");
    if (!access || !json_is_string(access)) {
        json_decref(root);
        error = "access_token missing";
        return false;
    }

    session.access_token = trim_copy(json_string_value(access));
    if (refresh && json_is_string(refresh))
        session.refresh_token = json_string_value(refresh);

    long long now = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    long long ttl = 604800;
    if (expires && json_is_integer(expires))
        ttl = json_integer_value(expires);
    session.expires_at = now + ttl - 60;

    json_decref(root);
    return true;
}

} // namespace

bool load_user_session(UserSession& out) {
    json_error_t jerr{};
    const std::string auth_path = paths::auth_json();
    json_t* root = json_load_file(auth_path.c_str(), 0, &jerr);
    if (!root)
        return false;
    json_t* a = json_object_get(root, "access_token");
    json_t* r = json_object_get(root, "refresh_token");
    json_t* e = json_object_get(root, "expires_at");
    if (a && json_is_string(a))
        out.access_token = trim_copy(json_string_value(a));
    if (r && json_is_string(r))
        out.refresh_token = trim_copy(json_string_value(r));
    if (e && json_is_integer(e))
        out.expires_at = json_integer_value(e);
    json_decref(root);
    return !out.access_token.empty();
}

bool delete_saved_session() {
    return std::remove(paths::auth_json().c_str()) == 0;
}

bool save_user_session(const UserSession& session) {
    json_t* root = json_object();
    json_object_set_new(root, "access_token", json_string(session.access_token.c_str()));
    json_object_set_new(root, "refresh_token", json_string(session.refresh_token.c_str()));
    json_object_set_new(root, "expires_at", json_integer(session.expires_at));
    const std::string save_path = paths::auth_json_save_path();
    if (json_dump_file(root, save_path.c_str(), JSON_INDENT(2)) != 0) {
        json_decref(root);
        return false;
    }
    json_decref(root);
    return true;
}

std::string build_oauth_login_page_url(const AppConfig& config, const std::string& ip) {
    std::string base = config.redirect_uri;
    const size_t slash = base.rfind('/');
    if (slash != std::string::npos)
        base = base.substr(0, slash + 1) + "login.html";
    else
        base = "https://stimpydev.github.io/switch-discord/login.html";
    std::ostringstream oss;
    oss << base << "?ip=" << url_encode(ip) << "&client_id=" << url_encode(config.client_id)
        << "&scope=" << url_encode(config.oauth_scopes);
    return oss.str();
}

std::string build_authorize_url(const AppConfig& config, const std::string& code_challenge,
                                const std::string& state) {
    std::ostringstream oss;
    oss << "https://discord.com/oauth2/authorize"
        << "?client_id=" << url_encode(config.client_id)
        << "&redirect_uri=" << url_encode(config.redirect_uri)
        << "&response_type=code"
        << "&scope=" << url_encode(config.oauth_scopes)
        << "&state=" << url_encode(state)
        << "&code_challenge=" << url_encode(code_challenge)
        << "&code_challenge_method=S256";
    return oss.str();
}

bool refresh_user_access_token(const AppConfig& config, UserSession& session, std::string& error) {
    if (session.refresh_token.empty() && config.refresh_token.empty()) {
        error = "Not logged in";
        return false;
    }

    std::string refresh =
        !session.refresh_token.empty() ? session.refresh_token : config.refresh_token;
    std::ostringstream body;
    body << "grant_type=refresh_token"
         << "&refresh_token=" << url_encode(refresh)
         << "&client_id=" << url_encode(config.client_id);
    if (!config.client_secret.empty())
        body << "&client_secret=" << url_encode(config.client_secret);

    if (!token_request(config, body.str(), session, error))
        return false;
    return save_user_session(session);
}

bool ensure_user_access_token(const AppConfig& config, UserSession& session, std::string& error) {
    long long now = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    if (!session.access_token.empty() && session.expires_at > now)
        return true;

    return refresh_user_access_token(config, session, error);
}

bool oauth_begin(const AppConfig& config, OAuthPending& pending, std::string& error) {
    if (config.client_id.empty() || config.redirect_uri.empty()) {
        error = "config.ini: client_id and redirect_uri required for user login";
        return false;
    }

    std::string ip;
    if (!get_switch_ipv4(ip, error))
        return false;

    pending.code_verifier = random_verifier(64);
    pending.switch_ip = ip;
    pending.authorize_url = build_oauth_login_page_url(config, ip);

    pending.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (pending.listen_fd < 0) {
        error = "socket() failed";
        return false;
    }
    int opt = 1;
    setsockopt(pending.listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kCallbackPort);

    if (bind(pending.listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        oauth_cancel(pending);
        error = "bind() failed (port 8765 in use?)";
        return false;
    }
    if (listen(pending.listen_fd, 1) < 0) {
        oauth_cancel(pending);
        error = "listen() failed";
        return false;
    }
    return true;
}

void oauth_cancel(OAuthPending& pending) {
    if (pending.listen_fd >= 0) {
        close(pending.listen_fd);
        pending.listen_fd = -1;
    }
}

bool oauth_wait_code(OAuthPending& pending, std::string& code, std::string& code_verifier_out,
                     std::string& error) {
    if (pending.listen_fd < 0) {
        error = "OAuth listener not started";
        return false;
    }
    error.clear();
    code_verifier_out.clear();
    if (!accept_oauth_code(pending.listen_fd, code, code_verifier_out, error))
        return false;
    oauth_cancel(pending);
    return true;
}

bool oauth_exchange_code(const AppConfig& config, const std::string& code,
                         const std::string& code_verifier, UserSession& session,
                         std::string& error) {
    std::ostringstream body;
    body << "grant_type=authorization_code"
         << "&code=" << url_encode(code)
         << "&redirect_uri=" << url_encode(config.redirect_uri)
         << "&client_id=" << url_encode(config.client_id)
         << "&code_verifier=" << url_encode(code_verifier);
    if (!config.client_secret.empty())
        body << "&client_secret=" << url_encode(config.client_secret);

    if (!token_request(config, body.str(), session, error))
        return false;
    if (!save_user_session(session)) {
        error = "Failed to save auth.json";
        return false;
    }
    return true;
}
