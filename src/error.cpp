#include "rdp/sdk/client.hpp"

namespace raft::rdp {

Error::Error(const std::string& message) : std::runtime_error(message) {}

} // namespace raft::rdp
