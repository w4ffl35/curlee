#pragma once

#include <string>
#include <unordered_set>

/**
 * @file capabilities.h
 * @brief Host-supplied capability set used to gate unsafe or host interactions.
 */

namespace curlee::runtime
{

/**
 * @brief Capabilities are explicit, opt-in permissions granted by the host (e.g. CLI).
 *
 * By default, no capabilities are granted.
 */
using Capabilities = std::unordered_set<std::string>;

} // namespace curlee::runtime
