#pragma once
#include "snatch_params.hpp"
#include <string>
namespace snatch { std::string serialize(const Params&); bool deserialize(const std::string&, Params&); }