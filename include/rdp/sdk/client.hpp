#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if RDP_SDK_ENABLE_WDM
#include "raft/wdm/v1/service/action_service.grpc.pb.h"
#include "raft/wdm/v1/service/object_service.grpc.pb.h"
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>
#endif

namespace raft::rdp {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

using Logger = std::function<void(LogLevel, const std::string&)>;

class Error : public std::runtime_error {
  public:
    explicit Error(const std::string& message);
};

struct ClientCredentials {
    std::string client_id;
    std::string client_secret;
};

struct Config {
    std::string server_url;
    std::string api_key;
    ClientCredentials client_credentials;
    std::string bearer_token;
    bool has_client_credentials = false;
    bool tls_skip_verify = false;
    Logger logger;
};

struct ClientOptions {
    std::string endpoint;
    std::string api_key;
    ClientCredentials client_credentials;
    std::string bearer_token;
    bool has_client_credentials = false;
    bool tls_skip_verify = false;
    std::chrono::milliseconds timeout{0};
    Logger logger;
};

using Option = std::function<void(ClientOptions&)>;
using ConfigOption = std::function<void(Config&)>;

struct RestResponse {
    long status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

std::string Version();
std::string DefaultEndpoint();

Config LoadConfig(std::vector<ConfigOption> options = {});

Option WithAPIKey(std::string api_key);
Option WithBearerToken(std::string token);
Option WithClientCredentials(std::string client_id, std::string client_secret);
Option WithTimeout(std::chrono::milliseconds timeout);
Option WithTLSSkipVerify(bool enabled = true);
Option WithLogger(Logger logger);
Option WithEndpoint(std::string endpoint);

ConfigOption WithConfigLogger(Logger logger);
ConfigOption WithConfigEndpoint(std::string endpoint);

class CatalogClient;
class PipelinesClient;
class TransformersClient;

class Client {
  public:
    explicit Client(std::string endpoint = "", std::vector<Option> options = {});
    explicit Client(ClientOptions options);

    static Client FromConfig(const Config& config, std::vector<Option> options = {});

    const std::string& endpoint() const;

    RestResponse Request(const std::string& method, const std::string& path, const std::string& body = "",
                         std::map<std::string, std::string> headers = {}) const;

    CatalogClient Catalog() const;
    PipelinesClient Pipelines() const;
    TransformersClient Transformers() const;

#if RDP_SDK_ENABLE_WDM
    std::string InvokeUnaryRpc(const std::string& full_method, const std::string& serialized_request) const;
    std::unique_ptr<grpc::ClientContext> NewGrpcContext() const;
    raft::wdm::v1::service::ObjectService::Stub& ObjectService();
    raft::wdm::v1::service::ActionService::Stub& ActionService();
#endif

  private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

class RestClient {
  public:
    RestClient(Client client, std::string base_path);

    RestResponse Get(const std::string& path = "", std::map<std::string, std::string> headers = {}) const;
    RestResponse Post(const std::string& path, const std::string& body,
                      std::map<std::string, std::string> headers = {}) const;
    RestResponse Put(const std::string& path, const std::string& body,
                     std::map<std::string, std::string> headers = {}) const;
    RestResponse Delete(const std::string& path, std::map<std::string, std::string> headers = {}) const;

  protected:
    const Client& client() const;
    std::string path(const std::string& suffix) const;

  private:
    Client client_;
    std::string base_path_;
};

class CatalogClient : public RestClient {
  public:
    explicit CatalogClient(Client client);
};

class PipelinesClient : public RestClient {
  public:
    explicit PipelinesClient(Client client);
};

class TransformersClient : public RestClient {
  public:
    explicit TransformersClient(Client client);
};

} // namespace raft::rdp
