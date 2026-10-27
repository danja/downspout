#pragma once
#include "tops_params.hpp"
#include <string>
namespace tops { std::string serialize(const Params&); bool deserialize(const std::string&, Params&); }