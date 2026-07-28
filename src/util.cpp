#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace raft::rdp::internal {

std::string Trim(std::string value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string GetEnv(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : std::string(value);
}

bool EnvBool(const std::string& value) { return ToLower(Trim(value)) == "true"; }

std::string UrlEncode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << static_cast<char>(c);
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return out.str();
}

void Log(const Logger& logger, LogLevel level, const std::string& message) {
    if (logger) {
        logger(level, message);
    }
}

Logger LoggerFromEnvironment(Logger injected) {
    if (injected) {
        return injected;
    }

    const auto level = ToLower(Trim(GetEnv("SDK_LOG_LEVEL")));
    if (level.empty()) {
        return nullptr;
    }

    return [level](LogLevel message_level, const std::string& message) {
        auto rank = [](LogLevel l) {
            switch (l) {
            case LogLevel::Debug:
                return 0;
            case LogLevel::Info:
                return 1;
            case LogLevel::Warn:
                return 2;
            case LogLevel::Error:
                return 3;
            }
            return 3;
        };

        int threshold = 4;
        if (level == "debug") {
            threshold = 0;
        } else if (level == "info") {
            threshold = 1;
        } else if (level == "warn") {
            threshold = 2;
        } else if (level == "error") {
            threshold = 3;
        }

        if (rank(message_level) >= threshold) {
            std::fprintf(stderr, "rdp-sdk-cpp: %s\n", message.c_str());
        }
    };
}

} // namespace raft::rdp::internal
