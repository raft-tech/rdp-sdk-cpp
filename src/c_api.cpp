#include "rdp/sdk.h"

#include "rdp/sdk/client.hpp"

#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>

struct rdp_config {
    raft::rdp::Config value;
};

struct rdp_client {
    std::unique_ptr<raft::rdp::Client> value;
};

struct rdp_error {
    std::string message;
};

namespace {

void set_error(rdp_error_t** out, const std::string& message) {
    if (out == nullptr) {
        return;
    }
    *out = new rdp_error{message};
}

int guard(rdp_error_t** err, const std::function<void()>& fn) {
    try {
        fn();
        return 0;
    } catch (const std::exception& ex) {
        set_error(err, ex.what());
        return 1;
    } catch (...) {
        set_error(err, "rdp: unknown error");
        return 1;
    }
}

std::string str(const char* value) { return value == nullptr ? std::string() : std::string(value); }

} // namespace

const char* rdp_version(void) {
    static const std::string version = raft::rdp::Version();
    return version.c_str();
}

rdp_config_t* rdp_config_new(void) {
    try {
        auto* config = new rdp_config;
        config->value.server_url = raft::rdp::DefaultEndpoint();
        return config;
    } catch (...) {
        return nullptr;
    }
}

int rdp_config_load(rdp_config_t** out, rdp_error_t** err) {
    return guard(err, [out] {
        if (out == nullptr) {
            throw raft::rdp::Error("rdp: config output pointer is null");
        }
        auto config = std::make_unique<rdp_config>();
        config->value = raft::rdp::LoadConfig();
        *out = config.release();
    });
}

void rdp_config_free(rdp_config_t* config) { delete config; }

int rdp_config_set_server_url(rdp_config_t* config, const char* value, rdp_error_t** err) {
    return guard(err, [config, value] {
        if (config == nullptr) {
            throw raft::rdp::Error("rdp: config is null");
        }
        config->value.server_url = str(value);
    });
}

int rdp_config_set_api_key(rdp_config_t* config, const char* value, rdp_error_t** err) {
    return guard(err, [config, value] {
        if (config == nullptr) {
            throw raft::rdp::Error("rdp: config is null");
        }
        config->value.api_key = str(value);
        config->value.bearer_token.clear();
        config->value.client_credentials = {};
        config->value.has_client_credentials = false;
    });
}

int rdp_config_set_bearer_token(rdp_config_t* config, const char* value, rdp_error_t** err) {
    return guard(err, [config, value] {
        if (config == nullptr) {
            throw raft::rdp::Error("rdp: config is null");
        }
        config->value.bearer_token = str(value);
        config->value.api_key.clear();
        config->value.client_credentials = {};
        config->value.has_client_credentials = false;
    });
}

int rdp_config_set_client_credentials(rdp_config_t* config, const char* client_id, const char* client_secret,
                                      rdp_error_t** err) {
    return guard(err, [config, client_id, client_secret] {
        if (config == nullptr) {
            throw raft::rdp::Error("rdp: config is null");
        }
        config->value.client_credentials = raft::rdp::ClientCredentials{str(client_id), str(client_secret)};
        config->value.has_client_credentials = true;
        config->value.api_key.clear();
        config->value.bearer_token.clear();
    });
}

void rdp_config_set_tls_skip_verify(rdp_config_t* config, bool enabled) {
    if (config != nullptr) {
        config->value.tls_skip_verify = enabled;
    }
}

int rdp_client_new(const rdp_config_t* config, rdp_client_t** out, rdp_error_t** err) {
    return guard(err, [config, out] {
        if (config == nullptr) {
            throw raft::rdp::Error("rdp: config is null");
        }
        if (out == nullptr) {
            throw raft::rdp::Error("rdp: client output pointer is null");
        }
        auto client = std::make_unique<rdp_client>();
        client->value = std::make_unique<raft::rdp::Client>(raft::rdp::Client::FromConfig(config->value));
        *out = client.release();
    });
}

void rdp_client_free(rdp_client_t* client) { delete client; }

int rdp_client_request(rdp_client_t* client, const char* method, const char* path, const char* body, rdp_buffer_t* out,
                       long* status_code, rdp_error_t** err) {
    return guard(err, [client, method, path, body, out, status_code] {
        if (client == nullptr || client->value == nullptr) {
            throw raft::rdp::Error("rdp: client is null");
        }
        if (out == nullptr) {
            throw raft::rdp::Error("rdp: response output pointer is null");
        }
        const auto response = client->value->Request(str(method), str(path), str(body));
        if (status_code != nullptr) {
            *status_code = response.status_code;
        }
        out->len = response.body.size();
        out->data = new char[out->len + 1];
        std::memcpy(out->data, response.body.data(), out->len);
        out->data[out->len] = '\0';
    });
}

int rdp_client_rpc_unary(rdp_client_t* client, const char* full_method, const void* request_data, size_t request_len,
                         rdp_buffer_t* out, rdp_error_t** err) {
    return guard(err, [client, full_method, request_data, request_len, out] {
        if (client == nullptr || client->value == nullptr) {
            throw raft::rdp::Error("rdp: client is null");
        }
        if (out == nullptr) {
            throw raft::rdp::Error("rdp: response output pointer is null");
        }
        if (request_data == nullptr && request_len != 0) {
            throw raft::rdp::Error("rdp: request data is null");
        }
#if RDP_SDK_ENABLE_WDM
        const auto request = std::string(static_cast<const char*>(request_data), request_len);
        const auto response = client->value->InvokeUnaryRpc(str(full_method), request);
        out->len = response.size();
        out->data = new char[out->len + 1];
        std::memcpy(out->data, response.data(), out->len);
        out->data[out->len] = '\0';
#else
        (void)full_method;
        throw raft::rdp::Error("rdp: WDM RPC support is not enabled in this build");
#endif
    });
}

void rdp_buffer_free(rdp_buffer_t* buffer) {
    if (buffer == nullptr) {
        return;
    }
    delete[] buffer->data;
    buffer->data = nullptr;
    buffer->len = 0;
}

const char* rdp_error_message(const rdp_error_t* err) {
    if (err == nullptr) {
        return "";
    }
    return err->message.c_str();
}

void rdp_error_free(rdp_error_t* err) { delete err; }
