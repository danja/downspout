#pragma once

#include "fontana_core_types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace downspout::fontana {

struct SampleLoadResult {
    SampleSource source;
    std::string error;
};

struct Peak {
    float min = 0.0f;
    float max = 0.0f;
};

[[nodiscard]] SampleLoadResult loadSampleSource(const std::string& path);
[[nodiscard]] SampleLoadResult loadOggSource(const std::string& path);

// Convenience used by the file browser preview: WAV/Ogg directly, or the
// first decoded sample of an SF2/SF3 bank.
[[nodiscard]] SampleLoadResult loadPreviewSource(const std::string& path);

[[nodiscard]] bool isAudioFile(const std::string& path);
[[nodiscard]] bool isSoundFontPath(const std::string& path);

[[nodiscard]] std::vector<Peak> computePeaks(const SampleSource& source, std::size_t bins);
[[nodiscard]] std::string describeSource(const SampleSource& source);

}  // namespace downspout::fontana
