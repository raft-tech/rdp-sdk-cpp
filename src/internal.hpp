#pragma once

#include "rdp/sdk/client.hpp"

#include <ctime>
#include <mutex>
#include <optional>
#include <string>

namespace raft::rdp::internal {

enum class AuthKind {
    None,
    APIKey,
    ClientCredentials,
    Bearer,
};

struct ResolvedAuth {
    AuthKind kind = AuthKind::None;
    std::string api_key;
    ClientCredentials client_credentials;
    std::string bearer_token;
};

struct NormalizedEndpoint {
    std::string url;
    std::string scheme;
    std::string host;
    int port = 0;
};

struct HttpRequest {
    std::string method;
    std::string url;
    std::string body;
    std::map<std::string, std::string> headers;
    bool tls_skip_verify = false;
    std::chrono::milliseconds timeout{0};
};

struct Token {
    std::string access_token;
    std::time_t expires_at = 0;
};

class TokenCache {
public:
    TokenCache() = default;
    TokenCache(std::string endpoint, ClientCredentials credentials, bool tls_skip_verify, Logger logger);

    std::string GetToken() const;

private:
    Token FetchToken() const;

    std::string endpoint_;
    ClientCredentials credentials_;
    bool tls_skip_verify_ = false;
    Logger logger_;
    mutable std::mutex mutex_;
    mutable Token cached_;
};

std::string Trim(std::string value);
std::string ToLower(std::string value);
std::string GetEnv(const char* name);
bool EnvBool(const std::string& value);
std::string UrlEncode(const std::string& value);

void Log(const Logger& logger, LogLevel level, const std::string& message);
Logger LoggerFromEnvironment(Logger injected = nullptr);

NormalizedEndpoint NormalizeEndpoint(std::string raw);
std::string ApplyPortOverride(const std::string& endpoint, const std::string& port);

ResolvedAuth ValidateAuth(
    const ClientCredentials& credentials,
    bool has_client_credentials,
    const std::string& api_key,
    const std::string& bearer_token,
    const Logger& logger
);

RestResponse PerformHttpRequest(const HttpRequest& request);
std::string ExtractJsonString(const std::string& json, const std::string& key);
int ExtractJsonInt(const std::string& json, const std::string& key, int default_value);

}  // namespace raft::rdp::internal
