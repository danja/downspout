#pragma once

#include "fontana_core_types.hpp"

#include <cstdint>

namespace downspout::fontana {

// All sample/bank data is owned by immutable shared pointers in the wrapper and
// borrowed here for the duration of one block. No allocation happens in run().
void processBlock(EngineState& state,
                  Parameters& parameters,
                  const SoundFont* font,
                  const SampleSource* sample,
                  const SliceSet* slices,
                  const std::uint32_t previewSerial,
                  const MidiInputEvent* midiEvents,
                  std::uint32_t midiCount,
                  std::uint32_t frames,
                  double hostSampleRate,
                  AudioBlock audio);

// Merges preset and instrument zone generators/modulators for one note.
struct ResolvedVoice {
    bool found = false;
    int sampleIndex = -1;
    GenSet generators;
    std::vector<Modulator> modulators;
};

[[nodiscard]] ResolvedVoice resolveNote(const SoundFont& font,
                                        int bank,
                                        int program,
                                        int note,
                                        int velocity);

}  // namespace downspout::fontana
