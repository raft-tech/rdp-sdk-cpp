#include "internal.hpp"

#include <ctime>

namespace raft::rdp {

Option WithAPIKey(std::string api_key) {
    return [api_key = std::move(api_key)](ClientOptions& options) {
        const auto trimmed = internal::Trim(api_key);
        if (trimmed.empty()) {
            return;
        }
        options.api_key = trimmed;
        options.bearer_token.clear();
        options.client_credentials = {};
        options.has_client_credentials = false;
    };
}

Option WithBearerToken(std::string token) {
    return [token = std::move(token)](ClientOptions& options) {
        const auto trimmed = internal::Trim(token);
        if (trimmed.empty()) {
            return;
        }
        options.bearer_token = trimmed;
        options.api_key.clear();
        options.client_credentials = {};
        options.has_client_credentials = false;
    };
}

Option WithClientCredentials(std::string client_id, std::string client_secret) {
    return [client_id = std::move(client_id), client_secret = std::move(client_secret)](ClientOptions& options) {
        const auto id = internal::Trim(client_id);
        const auto secret = internal::Trim(client_secret);
        if (id.empty() || secret.empty()) {
            return;
        }
        options.client_credentials = ClientCredentials{id, secret};
        options.has_client_credentials = true;
        options.api_key.clear();
        options.bearer_token.clear();
    };
}

Option WithTimeout(std::chrono::milliseconds timeout) {
    return [timeout](ClientOptions& options) { options.timeout = timeout; };
}

Option WithTLSSkipVerify(bool enabled) {
    return [enabled](ClientOptions& options) { options.tls_skip_verify = enabled; };
}

Option WithLogger(Logger logger) {
    return [logger = std::move(logger)](ClientOptions& options) { options.logger = logger; };
}

Option WithEndpoint(std::string endpoint) {
    return [endpoint = std::move(endpoint)](ClientOptions& options) {
        if (!internal::Trim(endpoint).empty()) {
            options.endpoint = internal::NormalizeEndpoint(endpoint).url;
        }
    };
}

} // namespace raft::rdp

namespace raft::rdp::internal {

ResolvedAuth ValidateAuth(const ClientCredentials& credentials, bool has_client_credentials, const std::string& api_key,
                          const std::string& bearer_token, const Logger& logger) {
    const auto trimmed_api_key = Trim(api_key);
    if (!api_key.empty() && trimmed_api_key.empty()) {
        Log(logger, LogLevel::Warn, "rdp: api_key is empty/whitespace, treating as unset");
    }

    const auto trimmed_bearer = Trim(bearer_token);
    if (!bearer_token.empty() && trimmed_bearer.empty()) {
        Log(logger, LogLevel::Warn, "rdp: bearer_token is empty/whitespace, treating as unset");
    }

    ClientCredentials trimmed_credentials{Trim(credentials.client_id), Trim(credentials.client_secret)};
    bool effective_credentials = false;
    if (has_client_credentials || !credentials.client_id.empty() || !credentials.client_secret.empty()) {
        if (trimmed_credentials.client_id.empty() && trimmed_credentials.client_secret.empty()) {
            effective_credentials = false;
        } else if (trimmed_credentials.client_id.empty() || trimmed_credentials.client_secret.empty()) {
            Log(logger, LogLevel::Warn, "rdp: incomplete client credentials, ignoring");
        } else {
            effective_credentials = true;
        }
    }

    const auto count =
        (trimmed_api_key.empty() ? 0 : 1) + (trimmed_bearer.empty() ? 0 : 1) + (effective_credentials ? 1 : 0);
    if (count > 1) {
        throw Error("rdp: provide exactly one auth method");
    }

    ResolvedAuth auth;
    if (!trimmed_api_key.empty()) {
        auth.kind = AuthKind::APIKey;
        auth.api_key = trimmed_api_key;
        return auth;
    }
    if (effective_credentials) {
        auth.kind = AuthKind::ClientCredentials;
        auth.client_credentials = trimmed_credentials;
        return auth;
    }
    if (!trimmed_bearer.empty()) {
        auth.kind = AuthKind::Bearer;
        auth.bearer_token = trimmed_bearer;
        return auth;
    }

    Log(logger, LogLevel::Warn, "rdp: no auth configured");
    return auth;
}

TokenCache::TokenCache(std::string endpoint, ClientCredentials credentials, bool tls_skip_verify, Logger logger)
    : endpoint_(std::move(endpoint)), credentials_(std::move(credentials)), tls_skip_verify_(tls_skip_verify),
      logger_(std::move(logger)) {}

std::string TokenCache::GetToken() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::time(nullptr);
    if (!cached_.access_token.empty() && cached_.expires_at > now + 30) {
        return cached_.access_token;
    }
    cached_ = FetchToken();
    return cached_.access_token;
}

Token TokenCache::FetchToken() const {
    Log(logger_, LogLevel::Debug, "rdp: fetching OAuth2 token");
    const auto body = std::string("grant_type=client_credentials&client_id=") + UrlEncode(credentials_.client_id) +
                      "&client_secret=" + UrlEncode(credentials_.client_secret);

    HttpRequest request;
    request.method = "POST";
    request.url = endpoint_ + "/api/v1/auth/token";
    request.body = body;
    request.tls_skip_verify = tls_skip_verify_;
    request.timeout = std::chrono::seconds(10);
    request.headers.emplace("Content-Type", "application/x-www-form-urlencoded");
    request.headers.emplace("Accept", "application/json");

    auto response = PerformHttpRequest(request);
    if (response.status_code < 200 || response.status_code >= 300) {
        throw Error("rdp: OAuth2 token fetch failed with HTTP " + std::to_string(response.status_code));
    }

    Token token;
    token.access_token = ExtractJsonString(response.body, "access_token");
    if (token.access_token.empty()) {
        throw Error("rdp: OAuth2 token response missing access_token");
    }

    const auto expires_in = ExtractJsonInt(response.body, "expires_in", 3600);
    token.expires_at = std::time(nullptr) + expires_in;
    return token;
}

} // namespace raft::rdp::internal
