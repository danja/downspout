#pragma once

#include "rift_core_types.hpp"

#include <string>

namespace downspout::rift {

struct SampleLoadResult {
    SampleSource source;
    std::string error;
};

[[nodiscard]] SampleLoadResult loadWavSampleSource(const std::string& path, double loopBeats);

}  // namespace downspout::rift
