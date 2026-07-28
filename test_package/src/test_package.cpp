#include "rdp/sdk/client.hpp"

int main() {
    auto client = raft::rdp::Client("https://rdp.local", {raft::rdp::WithAPIKey("api-key")});
    return client.endpoint() == "https://rdp.local" ? 0 : 1;
}
