#pragma once

#include <string>
#include <unordered_set>

namespace curlee::runtime
{

// Capabilities are explicit, opt-in permissions granted by the host (e.g. CLI).
// By default, no capabilities are granted.
using Capabilities = std::unordered_set<std::string>;

} // namespace curlee::runtime
