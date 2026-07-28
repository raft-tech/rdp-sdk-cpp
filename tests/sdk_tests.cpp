#include "rdp/sdk/client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

class EnvGuard {
  public:
    explicit EnvGuard(std::vector<std::string> names) : names_(std::move(names)) {
        for (const auto& name : names_) {
            const char* value = std::getenv(name.c_str());
            values_.push_back(value == nullptr ? std::string() : std::string(value));
            had_value_.push_back(value != nullptr);
        }
    }

    ~EnvGuard() {
        for (std::size_t i = 0; i < names_.size(); ++i) {
            if (had_value_[i]) {
                setenv(names_[i].c_str(), values_[i].c_str(), 1);
            } else {
                unsetenv(names_[i].c_str());
            }
        }
    }

  private:
    std::vector<std::string> names_;
    std::vector<std::string> values_;
    std::vector<bool> had_value_;
};

class HttpServer {
  public:
    using Handler = std::function<std::string(const std::string&)>;

    explicit HttpServer(std::vector<Handler> handlers) : handlers_(std::move(handlers)) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd_ >= 0);

        int enabled = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        assert(bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        assert(listen(fd_, 8) == 0);

        socklen_t len = sizeof(addr);
        assert(getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);

        thread_ = std::thread([this] { serve(); });
    }

    ~HttpServer() {
        if (fd_ >= 0) {
            close(fd_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

  private:
    void serve() {
        for (auto& handler : handlers_) {
            const int conn = accept(fd_, nullptr, nullptr);
            if (conn < 0) {
                return;
            }

            std::string request;
            char buffer[4096];
            const ssize_t n = read(conn, buffer, sizeof(buffer));
            if (n > 0) {
                request.assign(buffer, static_cast<std::size_t>(n));
            }

            const auto body = handler(request);
            const auto response =
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                "\r\nConnection: close\r\n\r\n" + body;
            (void)write(conn, response.data(), response.size());
            close(conn);
        }
    }

    int fd_ = -1;
    unsigned short port_ = 0;
    std::vector<Handler> handlers_;
    std::thread thread_;
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void test_bearer_env_loads() {
    EnvGuard guard({"RDP_SERVER_URL", "RDP_BEARER_TOKEN", "RDP_API_KEY", "RDP_CLIENT_ID", "RDP_CLIENT_SECRET"});
    setenv("RDP_SERVER_URL", "https://rdp.example.com", 1);
    setenv("RDP_BEARER_TOKEN", " tok-123 ", 1);
    unsetenv("RDP_API_KEY");
    unsetenv("RDP_CLIENT_ID");
    unsetenv("RDP_CLIENT_SECRET");

    auto cfg = raft::rdp::LoadConfig();
    assert(cfg.server_url == "https://rdp.example.com");
    assert(cfg.bearer_token == "tok-123");
}

void test_auth_conflicts_error() {
    EnvGuard guard({"RDP_SERVER_URL", "RDP_BEARER_TOKEN", "RDP_API_KEY", "RDP_CLIENT_ID", "RDP_CLIENT_SECRET"});
    setenv("RDP_API_KEY", "ak", 1);
    setenv("RDP_BEARER_TOKEN", "tok", 1);
    unsetenv("RDP_CLIENT_ID");
    unsetenv("RDP_CLIENT_SECRET");

    bool threw = false;
    try {
        (void)raft::rdp::LoadConfig();
    } catch (const raft::rdp::Error&) {
        threw = true;
    }
    assert(threw);
}

void test_blank_bearer_warns_and_falls_through() {
    EnvGuard guard({"RDP_BEARER_TOKEN", "RDP_API_KEY", "RDP_CLIENT_ID", "RDP_CLIENT_SECRET"});
    setenv("RDP_BEARER_TOKEN", "   ", 1);
    unsetenv("RDP_API_KEY");
    unsetenv("RDP_CLIENT_ID");
    unsetenv("RDP_CLIENT_SECRET");

    std::vector<std::string> warnings;
    auto cfg = raft::rdp::LoadConfig({
        raft::rdp::WithConfigLogger([&warnings](raft::rdp::LogLevel level, const std::string& message) {
            if (level == raft::rdp::LogLevel::Warn) {
                warnings.push_back(message);
            }
        }),
    });

    assert(cfg.bearer_token.empty());
    bool saw_bearer = false;
    for (const auto& warning : warnings) {
        saw_bearer = saw_bearer || contains(warning, "bearer_token");
    }
    assert(saw_bearer);
}

void test_api_key_header() {
    HttpServer server({
        [](const std::string& request) {
            assert(contains(request, "GET /api/v2/catalog/datasources HTTP/1.1"));
            assert(contains(request, "X-Api-Key: ak-123"));
            return R"({"ok":true})";
        },
    });

    raft::rdp::Client client(server.url(), {raft::rdp::WithAPIKey("ak-123")});
    const auto response = client.Catalog().Get("/datasources");
    assert(response.status_code == 200);
    assert(response.body == R"({"ok":true})");
}

void test_bearer_header_and_override() {
    HttpServer server({
        [](const std::string& request) {
            assert(contains(request, "Authorization: Bearer tok-override"));
            assert(!contains(request, "X-Api-Key:"));
            return R"({"ok":true})";
        },
    });

    raft::rdp::Config cfg;
    cfg.server_url = server.url();
    cfg.api_key = "ak-config";

    auto client = raft::rdp::Client::FromConfig(cfg, {raft::rdp::WithBearerToken("tok-override")});
    const auto response = client.Catalog().Get("/datasources");
    assert(response.status_code == 200);
}

void test_oauth_fetches_and_sends_bearer() {
    HttpServer server({
        [](const std::string& request) {
            assert(contains(request, "POST /api/v1/auth/token HTTP/1.1"));
            assert(contains(request, "grant_type=client_credentials"));
            assert(contains(request, "client_id=cid"));
            assert(contains(request, "client_secret=csec"));
            return R"({"access_token":"tok-oauth","token_type":"Bearer","expires_in":3600})";
        },
        [](const std::string& request) {
            assert(contains(request, "GET /api/v2/catalog/datasources HTTP/1.1"));
            assert(contains(request, "Authorization: Bearer tok-oauth"));
            return R"({"ok":true})";
        },
    });

    raft::rdp::Client client(server.url(), {raft::rdp::WithClientCredentials("cid", "csec")});
    const auto response = client.Catalog().Get("/datasources");
    assert(response.status_code == 200);
}

} // namespace

int main() {
    test_bearer_env_loads();
    test_auth_conflicts_error();
    test_blank_bearer_warns_and_falls_through();
    test_api_key_header();
    test_bearer_header_and_override();
    test_oauth_fetches_and_sends_bearer();

    std::cout << "sdk tests passed\n";
    return 0;
}
