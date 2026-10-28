#pragma once

#include "clatter_params.hpp"

#include <string>

namespace clatter {

std::string serialize(const Params& p);
// Replaces `p` only when the whole string is valid; out-of-range values are clamped, bad selectors rejected.
bool deserialize(const std::string& s, Params& p);

}  // namespace clatter
