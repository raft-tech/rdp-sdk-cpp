#include "internal.hpp"

#include <charconv>

namespace raft::rdp {

Config LoadConfig(std::vector<ConfigOption> options) {
    Config config;
    config.logger = internal::LoggerFromEnvironment();

    config.server_url = internal::ApplyPortOverride(internal::NormalizeEndpoint(internal::GetEnv("RDP_SERVER_URL")).url,
                                                    internal::GetEnv("RDP_SERVER_PORT"));

    config.api_key = internal::Trim(internal::GetEnv("RDP_API_KEY"));
    config.client_credentials.client_id = internal::GetEnv("RDP_CLIENT_ID");
    config.client_credentials.client_secret = internal::GetEnv("RDP_CLIENT_SECRET");
    config.has_client_credentials =
        !config.client_credentials.client_id.empty() || !config.client_credentials.client_secret.empty();
    config.bearer_token = internal::GetEnv("RDP_BEARER_TOKEN");
    config.tls_skip_verify = internal::EnvBool(internal::GetEnv("TLS_SKIP_VERIFY"));

    for (const auto& option : options) {
        option(config);
    }
    config.logger = internal::LoggerFromEnvironment(config.logger);

    auto auth = internal::ValidateAuth(config.client_credentials, config.has_client_credentials, config.api_key,
                                       config.bearer_token, config.logger);

    config.api_key.clear();
    config.bearer_token.clear();
    config.client_credentials = {};
    config.has_client_credentials = false;

    switch (auth.kind) {
    case internal::AuthKind::APIKey:
        config.api_key = auth.api_key;
        break;
    case internal::AuthKind::ClientCredentials:
        config.client_credentials = auth.client_credentials;
        config.has_client_credentials = true;
        break;
    case internal::AuthKind::Bearer:
        config.bearer_token = auth.bearer_token;
        break;
    case internal::AuthKind::None:
        break;
    }

    return config;
}

ConfigOption WithConfigLogger(Logger logger) {
    return [logger = std::move(logger)](Config& config) { config.logger = logger; };
}

ConfigOption WithConfigEndpoint(std::string endpoint) {
    return [endpoint = std::move(endpoint)](Config& config) {
        if (!internal::Trim(endpoint).empty()) {
            config.server_url = internal::NormalizeEndpoint(endpoint).url;
        }
    };
}

} // namespace raft::rdp

namespace raft::rdp::internal {

NormalizedEndpoint NormalizeEndpoint(std::string raw) {
    raw = Trim(raw);
    if (raw.empty()) {
        raw = DefaultEndpoint();
    }

    while (!raw.empty() && raw.back() == '/') {
        raw.pop_back();
    }

    const auto scheme_pos = raw.find("://");
    if (scheme_pos == std::string::npos || scheme_pos == 0) {
        throw Error("rdp: invalid endpoint: must be an absolute URL with scheme and host");
    }

    NormalizedEndpoint out;
    out.scheme = ToLower(raw.substr(0, scheme_pos));
    const auto after_scheme = scheme_pos + 3;
    const auto authority_end = raw.find_first_of("/?#", after_scheme);
    if (authority_end != std::string::npos) {
        throw Error("rdp: invalid endpoint: must not include path, query, or fragment");
    }

    auto authority = raw.substr(after_scheme);
    if (authority.empty()) {
        throw Error("rdp: invalid endpoint: must include host");
    }
    if (authority.find('@') != std::string::npos) {
        throw Error("rdp: invalid endpoint: must not include userinfo");
    }

    out.host = authority;
    out.url = raw;
    return out;
}

std::string ApplyPortOverride(const std::string& endpoint, const std::string& port_raw) {
    const auto port = Trim(port_raw);
    if (port.empty()) {
        return endpoint;
    }

    int value = 0;
    const auto* begin = port.data();
    const auto* end = port.data() + port.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value <= 0 || value > 65535) {
        throw Error("rdp: invalid RDP_SERVER_PORT: " + port);
    }

    const auto scheme_pos = endpoint.find("://");
    const auto authority_start = scheme_pos + 3;
    auto authority = endpoint.substr(authority_start);

    std::string host;
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        host = authority.substr(0, close + 1);
    } else {
        host = authority.substr(0, authority.find(':'));
    }

    return endpoint.substr(0, authority_start) + host + ":" + std::to_string(value);
}

} // namespace raft::rdp::internal
