# RDP SDK for C/C++

The RDP SDK for C/C++ is part of the unified SDK family for client
applications interacting with Raft Data Platform (RDP). It provides a C++17
API, a stable C ABI, environment-driven configuration, authentication, TLS,
timeout, WDM gRPC clients, and REST helpers for the RDP API surface.

For platform concepts, API guides, and integration details, see
https://developer.teamraft.com.

See [Terms of Use](https://developer.teamraft.com/terms-of-use/).

## Installation

With CMake:

```cmake
find_package(RdpSdkCpp CONFIG REQUIRED)

add_executable(my-rdp-client main.cpp)
target_link_libraries(my-rdp-client PRIVATE Rdp::Sdk)
```

With Conan:

```bash
conan install --requires=rdp-sdk-cpp/<version> --build=missing
```

The SDK can optionally enable WDM generated gRPC C++ clients at build time:

```bash
cmake -S . -B build -DRDP_SDK_ENABLE_WDM=ON
```

The generated WDM bindings are consumed from the
[`raft/wdm`](https://buf.build/raft/wdm) Buf module using pinned BSR CMake
SDK URLs.

## Quick Start

Build with WDM gRPC support enabled:

```bash
cmake -S . -B build -DRDP_SDK_ENABLE_WDM=ON
cmake --build build --parallel
```

```bash
export RDP_SERVER_URL=https://rdp.example.com
export RDP_API_KEY=your-api-key
```

C++:

```cpp
#include "rdp/sdk/client.hpp"

#include <chrono>
#include <iostream>

int main() {
    auto cfg = raft::rdp::LoadConfig();
    auto client = raft::rdp::Client::FromConfig(
        cfg,
        {raft::rdp::WithTimeout(std::chrono::seconds(10))}
    );

    raft::wdm::v1::service::SearchObjectsRequest request;
    request.set_page_size(10);

    raft::wdm::v1::service::SearchObjectsResponse response;
    auto context = client.NewGrpcContext();
    auto status = client.ObjectService().SearchObjects(context.get(), request, &response);
    if (!status.ok()) {
        std::cerr << "SearchObjects: " << status.error_message() << "\n";
        return 1;
    }

    if (response.objects().empty()) {
        std::cout << "No WDM objects found.\n";
        return 0;
    }

    for (int i = 0; i < response.objects_size(); ++i) {
        const auto& object = response.objects(i);
        std::cout << i + 1 << ". id=" << object.id()
                  << " name=\"" << object.name() << "\""
                  << " status=" << raft::wdm::v1::ObjectStatus_Name(object.status())
                  << "\n";
    }
}
```

C:

```c
#include "rdp/sdk.h"

#include <stdio.h>

int main(void) {
    rdp_error_t* err = 0;
    rdp_config_t* cfg = 0;
    rdp_client_t* client = 0;
    rdp_buffer_t body = {0};
    const unsigned char request[] = {0x18, 0x0a}; /* page_size = 10 */

    if (rdp_config_load(&cfg, &err) != 0 ||
        rdp_client_new(cfg, &client, &err) != 0 ||
        rdp_client_rpc_unary(
            client,
            "/raft.wdm.v1.service.ObjectService/SearchObjects",
            request,
            sizeof(request),
            &body,
            &err
        ) != 0) {
        fprintf(stderr, "%s\n", rdp_error_message(err));
        rdp_error_free(err);
        rdp_buffer_free(&body);
        rdp_client_free(client);
        rdp_config_free(cfg);
        return 1;
    }

    printf("SearchObjects response: %zu bytes\n", body.len);
    rdp_buffer_free(&body);
    rdp_client_free(client);
    rdp_config_free(cfg);
    return 0;
}
```

For more examples, see [examples](examples/).

## Configuration

`LoadConfig()` reads connection settings from environment variables and
returns a validated `Config` for `Client::FromConfig`. C callers use
`rdp_config_load`.

| Variable | Required | Description |
| --- | --- | --- |
| `RDP_SERVER_URL` | No | Base RDP endpoint. Defaults to `https://rdp.local`; use scheme and host only. |
| `RDP_SERVER_PORT` | No | Optional port override for the endpoint. |
| `TLS_SKIP_VERIFY` | No | Set to `true` only for development or test endpoints with self-signed certificates. |

### Authentication

API key authentication is preferred for service clients. Use OAuth2 client
credentials or a static Bearer token only when your deployment requires them.

| Method | Variables | Request behavior |
| --- | --- | --- |
| API key | `RDP_API_KEY` | Sends the value on each request as `X-Api-Key`. |
| OAuth2 client credentials | `RDP_CLIENT_ID`, `RDP_CLIENT_SECRET` | Fetches a token from `{RDP_SERVER_URL}/api/v1/auth/token` and sends it as a bearer token. |
| Bearer | `RDP_BEARER_TOKEN` | Sends `Authorization: Bearer <token>`. |

Providing more than one auth method is an error. If no method is configured,
requests are sent without auth and the SDK logs a warning when logging is
enabled.

### Functional Configuration

C++ callers pass functional options to `Client(endpoint, options)` or to
`Client::FromConfig(cfg, options)`. Call-site options are applied after loaded
configuration.

```cpp
auto client = raft::rdp::Client(
    "https://rdp.example.com",
    {
        raft::rdp::WithAPIKey("api-key"),
        raft::rdp::WithTimeout(std::chrono::seconds(10)),
    }
);
```

Use `WithClientCredentials(clientID, clientSecret)` only when your deployment
requires OAuth2 client credentials. Use `WithTLSSkipVerify()` only for
development or test endpoints with self-signed certificates.

### C ABI

C callers use `<rdp/sdk.h>` with opaque handles and explicit lifecycle
functions. The C ABI never exposes C++ types. Use `rdp_client_request` for raw
JSON REST calls, `rdp_client_rpc_unary` for serialized unary WDM RPC calls in
WDM-enabled builds, and `rdp_buffer_free` for response ownership.
