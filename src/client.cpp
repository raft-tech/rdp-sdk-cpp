#include "internal.hpp"

#include <utility>

namespace raft::rdp {

#if RDP_SDK_ENABLE_WDM
namespace {

std::string GrpcTarget(const std::string& endpoint) {
    const auto marker = std::string("://");
    const auto pos = endpoint.find(marker);
    return pos == std::string::npos ? endpoint : endpoint.substr(pos + marker.size());
}

} // namespace
#endif

class Client::Impl {
  public:
    explicit Impl(ClientOptions options)
        : endpoint_(internal::NormalizeEndpoint(options.endpoint).url), tls_skip_verify_(options.tls_skip_verify),
          timeout_(options.timeout), logger_(internal::LoggerFromEnvironment(options.logger)),
          auth_(internal::ValidateAuth(options.client_credentials, options.has_client_credentials, options.api_key,
                                       options.bearer_token, logger_)) {
        if (tls_skip_verify_) {
            internal::Log(logger_, LogLevel::Warn, "rdp: TLS certificate verification disabled (insecure)");
        }
        if (auth_.kind == internal::AuthKind::ClientCredentials) {
            token_cache_ =
                std::make_unique<internal::TokenCache>(endpoint_, auth_.client_credentials, tls_skip_verify_, logger_);
        }
    }

    const std::string& endpoint() const { return endpoint_; }

    RestResponse request(const std::string& method, const std::string& path, const std::string& body,
                         std::map<std::string, std::string> headers) const {
        headers.emplace("User-Agent", "rdp-sdk-cpp/" + Version());
        headers.emplace("Accept", "application/json");

        switch (auth_.kind) {
        case internal::AuthKind::APIKey:
            headers["X-Api-Key"] = auth_.api_key;
            break;
        case internal::AuthKind::ClientCredentials:
            headers["Authorization"] = "Bearer " + token_cache_->GetToken();
            break;
        case internal::AuthKind::Bearer:
            headers["Authorization"] = "Bearer " + auth_.bearer_token;
            break;
        case internal::AuthKind::None:
            break;
        }

        internal::HttpRequest request;
        request.method = method;
        request.url = endpoint_ + path;
        request.body = body;
        request.headers = std::move(headers);
        request.tls_skip_verify = tls_skip_verify_;
        request.timeout = timeout_;
        return internal::PerformHttpRequest(request);
    }

#if RDP_SDK_ENABLE_WDM
    std::string invoke_unary_rpc(const std::string& full_method, const std::string& serialized_request) const {
        if (full_method.empty() || full_method.front() != '/') {
            throw Error("rdp: RPC method must be a fully-qualified path, for example /package.Service/Method");
        }
        ensure_channel();

        auto context = new_grpc_context();
        grpc::GenericStub stub(channel_);
        grpc::Slice request_slice(serialized_request.data(), serialized_request.size());
        grpc::ByteBuffer request_buffer(&request_slice, 1);
        grpc::ByteBuffer response_buffer;

        grpc::CompletionQueue cq;
        auto rpc = stub.PrepareUnaryCall(context.get(), full_method, request_buffer, &cq);
        if (!rpc) {
            throw Error("rdp: failed to create RPC " + full_method);
        }

        grpc::Status status;
        void* tag = reinterpret_cast<void*>(1);
        rpc->StartCall();
        rpc->Finish(&response_buffer, &status, tag);

        void* completed_tag = nullptr;
        bool ok = false;
        if (!cq.Next(&completed_tag, &ok) || completed_tag != tag || !ok) {
            cq.Shutdown();
            throw Error("rdp: RPC " + full_method + " did not complete");
        }
        cq.Shutdown();

        if (!status.ok()) {
            throw Error("rdp: RPC " + full_method + " failed: " + std::to_string(status.error_code()) + " " +
                        status.error_message());
        }

        std::vector<grpc::Slice> response_slices;
        if (!response_buffer.Dump(&response_slices).ok()) {
            throw Error("rdp: failed to read RPC response bytes");
        }

        std::string response;
        for (const auto& slice : response_slices) {
            response.append(reinterpret_cast<const char*>(slice.begin()), slice.size());
        }
        return response;
    }

    std::unique_ptr<grpc::ClientContext> new_grpc_context() const {
        auto context = std::make_unique<grpc::ClientContext>();
        context->AddMetadata("user-agent", "rdp-sdk-cpp/" + Version());

        switch (auth_.kind) {
        case internal::AuthKind::APIKey:
            context->AddMetadata("x-api-key", auth_.api_key);
            break;
        case internal::AuthKind::ClientCredentials:
            context->AddMetadata("authorization", "Bearer " + token_cache_->GetToken());
            break;
        case internal::AuthKind::Bearer:
            context->AddMetadata("authorization", "Bearer " + auth_.bearer_token);
            break;
        case internal::AuthKind::None:
            break;
        }
        if (timeout_.count() > 0) {
            context->set_deadline(std::chrono::system_clock::now() + timeout_);
        }
        return context;
    }

    raft::wdm::v1::service::ObjectService::Stub& object_service() {
        ensure_channel();
        if (!object_service_) {
            object_service_ = raft::wdm::v1::service::ObjectService::NewStub(channel_);
        }
        return *object_service_;
    }

    raft::wdm::v1::service::ActionService::Stub& action_service() {
        ensure_channel();
        if (!action_service_) {
            action_service_ = raft::wdm::v1::service::ActionService::NewStub(channel_);
        }
        return *action_service_;
    }
#endif

  private:
#if RDP_SDK_ENABLE_WDM
    void ensure_channel() const {
        if (channel_) {
            return;
        }
        if (tls_skip_verify_) {
            internal::Log(logger_, LogLevel::Warn, "rdp: TLS skip verify is not available for gRPC C++ channels");
        }
        channel_ = grpc::CreateChannel(GrpcTarget(endpoint_), grpc::SslCredentials(grpc::SslCredentialsOptions{}));
    }
#endif

    std::string endpoint_;
    bool tls_skip_verify_ = false;
    std::chrono::milliseconds timeout_{0};
    Logger logger_;
    internal::ResolvedAuth auth_;
    std::unique_ptr<internal::TokenCache> token_cache_;

#if RDP_SDK_ENABLE_WDM
    mutable std::shared_ptr<grpc::Channel> channel_;
    mutable std::unique_ptr<raft::wdm::v1::service::ObjectService::Stub> object_service_;
    mutable std::unique_ptr<raft::wdm::v1::service::ActionService::Stub> action_service_;
#endif
};

Client::Client(std::string endpoint, std::vector<Option> options) {
    ClientOptions resolved;
    resolved.endpoint = std::move(endpoint);
    for (const auto& option : options) {
        option(resolved);
    }
    if (resolved.endpoint.empty()) {
        resolved.endpoint = DefaultEndpoint();
    }
    impl_ = std::make_shared<Impl>(std::move(resolved));
}

Client::Client(ClientOptions options) {
    if (options.endpoint.empty()) {
        options.endpoint = DefaultEndpoint();
    }
    impl_ = std::make_shared<Impl>(std::move(options));
}

Client Client::FromConfig(const Config& config, std::vector<Option> options) {
    ClientOptions resolved;
    resolved.endpoint = config.server_url;
    resolved.api_key = config.api_key;
    resolved.client_credentials = config.client_credentials;
    resolved.has_client_credentials = config.has_client_credentials;
    resolved.bearer_token = config.bearer_token;
    resolved.tls_skip_verify = config.tls_skip_verify;
    resolved.logger = config.logger;

    for (const auto& option : options) {
        option(resolved);
    }
    return Client(std::move(resolved));
}

const std::string& Client::endpoint() const { return impl_->endpoint(); }

RestResponse Client::Request(const std::string& method, const std::string& path, const std::string& body,
                             std::map<std::string, std::string> headers) const {
    if (path.empty() || path.front() != '/') {
        throw Error("rdp: request path must start with /");
    }
    return impl_->request(method, path, body, std::move(headers));
}

CatalogClient Client::Catalog() const { return CatalogClient(*this); }

PipelinesClient Client::Pipelines() const { return PipelinesClient(*this); }

TransformersClient Client::Transformers() const { return TransformersClient(*this); }

RestClient::RestClient(Client client, std::string base_path)
    : client_(std::move(client)), base_path_(std::move(base_path)) {}

RestResponse RestClient::Get(const std::string& suffix, std::map<std::string, std::string> headers) const {
    return client_.Request("GET", path(suffix), "", std::move(headers));
}

RestResponse RestClient::Post(const std::string& suffix, const std::string& body,
                              std::map<std::string, std::string> headers) const {
    headers.emplace("Content-Type", "application/json");
    return client_.Request("POST", path(suffix), body, std::move(headers));
}

RestResponse RestClient::Put(const std::string& suffix, const std::string& body,
                             std::map<std::string, std::string> headers) const {
    headers.emplace("Content-Type", "application/json");
    return client_.Request("PUT", path(suffix), body, std::move(headers));
}

RestResponse RestClient::Delete(const std::string& suffix, std::map<std::string, std::string> headers) const {
    return client_.Request("DELETE", path(suffix), "", std::move(headers));
}

const Client& RestClient::client() const { return client_; }

std::string RestClient::path(const std::string& suffix) const {
    if (suffix.empty()) {
        return base_path_;
    }
    if (suffix.front() == '/') {
        return base_path_ + suffix;
    }
    return base_path_ + "/" + suffix;
}

CatalogClient::CatalogClient(Client client) : RestClient(std::move(client), "/api/v2/catalog") {}

PipelinesClient::PipelinesClient(Client client) : RestClient(std::move(client), "/api/v2/pipelines") {}

TransformersClient::TransformersClient(Client client) : RestClient(std::move(client), "/api/v2/transformers") {}

#if RDP_SDK_ENABLE_WDM
std::string Client::InvokeUnaryRpc(const std::string& full_method, const std::string& serialized_request) const {
    return impl_->invoke_unary_rpc(full_method, serialized_request);
}

std::unique_ptr<grpc::ClientContext> Client::NewGrpcContext() const { return impl_->new_grpc_context(); }

raft::wdm::v1::service::ObjectService::Stub& Client::ObjectService() { return impl_->object_service(); }

raft::wdm::v1::service::ActionService::Stub& Client::ActionService() { return impl_->action_service(); }
#endif

} // namespace raft::rdp
