#include "rdp/sdk/client.hpp"

namespace raft::rdp {

std::string Version() { return RDP_SDK_VERSION; }

std::string DefaultEndpoint() { return "https://rdp.local"; }

} // namespace raft::rdp
