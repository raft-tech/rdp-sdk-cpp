#include "internal.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>

namespace raft::rdp::internal {

namespace {

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }

    ~CurlGlobal() { curl_global_cleanup(); }
};

CurlGlobal& GlobalCurl() {
    static CurlGlobal global;
    return global;
}

size_t WriteBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t WriteHeader(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(ptr, size * nmemb);
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return size * nmemb;
    }
    auto key = line.substr(0, colon);
    auto value = line.substr(colon + 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    (*headers)[key] = Trim(value);
    return size * nmemb;
}

struct HeaderListDeleter {
    void operator()(curl_slist* headers) const { curl_slist_free_all(headers); }
};

} // namespace

RestResponse PerformHttpRequest(const HttpRequest& request) {
    (void)GlobalCurl();

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) {
        throw Error("rdp: failed to initialize curl");
    }

    RestResponse response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, WriteHeader);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);

    if (request.timeout.count() > 0) {
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout.count()));
    }
    if (request.tls_skip_verify) {
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
    }

    std::unique_ptr<curl_slist, HeaderListDeleter> headers(nullptr);
    for (const auto& [key, value] : request.headers) {
        const auto line = key + ": " + value;
        headers.reset(curl_slist_append(headers.release(), line.c_str()));
    }
    if (headers) {
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    }

    const auto method = ToLower(request.method);
    if (method == "get") {
        curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
    } else if (method == "post") {
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    } else {
        const auto upper = request.method;
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, upper.c_str());
        if (!request.body.empty()) {
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }
    }

    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        throw Error(std::string("rdp: HTTP request failed: ") + curl_easy_strerror(code));
    }
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
    return response;
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    const auto marker = "\"" + key + "\"";
    auto pos = json.find(marker);
    if (pos == std::string::npos) {
        return "";
    }
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return "";
    }
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) {
        return "";
    }
    std::string out;
    bool escaped = false;
    for (auto i = pos + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (escaped) {
            out.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            return out;
        }
        out.push_back(c);
    }
    return "";
}

int ExtractJsonInt(const std::string& json, const std::string& key, int default_value) {
    const auto marker = "\"" + key + "\"";
    auto pos = json.find(marker);
    if (pos == std::string::npos) {
        return default_value;
    }
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return default_value;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    const auto start = pos;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (start == pos) {
        return default_value;
    }
    return std::stoi(json.substr(start, pos - start));
}

} // namespace raft::rdp::internal
