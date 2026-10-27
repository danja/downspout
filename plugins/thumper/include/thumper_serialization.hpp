#pragma once
#include "thumper_params.hpp"
#include <string>

namespace thumper {
std::string serialize(const Params& p);
bool deserialize(const std::string& s, Params& p);
} // namespace thumper