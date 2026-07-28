// Run:
//
//   cmake -S . -B build -DRDP_SDK_ENABLE_WDM=ON
//   cmake --build build --target objects-search-cpp
//   export RDP_SERVER_URL=https://rdp.example.com
//   export RDP_API_KEY=your-api-key
//   ./build/objects-search-cpp

#include "rdp/sdk/client.hpp"

#include <chrono>
#include <iostream>

#if !RDP_SDK_ENABLE_WDM
int main() {
    std::cerr << "objects-search-cpp requires a build with RDP_SDK_ENABLE_WDM=ON\n";
    return 2;
}
#else
int main() {
    try {
        auto cfg = raft::rdp::LoadConfig();
        auto client = raft::rdp::Client::FromConfig(cfg, {raft::rdp::WithTimeout(std::chrono::seconds(10))});

        raft::wdm::v1::service::SearchObjectsRequest request;
        request.set_page_size(10);

        raft::wdm::v1::service::SearchObjectsResponse response;
        auto context = client.NewGrpcContext();
        const auto status = client.ObjectService().SearchObjects(context.get(), request, &response);
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
            std::cout << i + 1 << ". id=" << object.id() << " name=\"" << object.name()
                      << "\" status=" << raft::wdm::v1::ObjectStatus_Name(object.status()) << "\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
#endif
