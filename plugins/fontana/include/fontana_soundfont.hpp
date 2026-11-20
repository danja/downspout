#pragma once

#include "fontana_core_types.hpp"

#include <string>

namespace downspout::fontana {

struct SoundFontLoadResult {
    SoundFont bank;
    std::string error;
};

// Parses RIFF sfbk (SF2 2.0-2.04 and SF3). SF3 Ogg streams are decoded with
// stb_vorbis. Parsing and decoding are deliberately host-independent so they
// can run on a UI/worker thread and be published atomically to the DSP.
[[nodiscard]] SoundFontLoadResult loadSoundFont(const std::string& path);

// True when a file starts with RIFF....sfbk.
[[nodiscard]] bool isSoundFontFile(const std::string& path);

}  // namespace downspout::fontana
