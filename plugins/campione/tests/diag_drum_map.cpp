// Diagnostic tool: loads a WAV, auto-slices at transients, runs drum map analysis,
// and prints full descriptor + score breakdown for each slice.
//
// Usage:  diag_drum_map <loop.wav> [num_slices]
//   num_slices = 0 (default) → auto-detect transients
//   num_slices > 0 → split evenly into that many slices

#include "campione_core_types.hpp"
#include "campione_drum_map.hpp"
#include "campione_pitch_utils.hpp"
#include "campione_sample_loader.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace downspout::campione;

static std::vector<SampleZone> sliceZone(const SampleZone& src, int numSlices)
{
    const int totalFrames = static_cast<int>(src.data.size() / src.channelCount);

    std::vector<uint32_t> boundaries;
    if (numSlices == 0) {
        const auto onsets = detectTransients(src.data, src.channelCount, src.sampleRate);
        if (onsets.empty()) {
            std::fprintf(stderr, "No transients detected — using whole file as one zone\n");
            return {src};
        }
        boundaries.push_back(0);
        for (uint32_t f : onsets) if (f > 0) boundaries.push_back(f);
        boundaries.push_back(static_cast<uint32_t>(totalFrames));
        numSlices = static_cast<int>(boundaries.size()) - 1;
        std::fprintf(stderr, "Auto-detected %d slices from %d transients\n",
                     numSlices, static_cast<int>(onsets.size()));
    } else {
        boundaries.resize(static_cast<std::size_t>(numSlices + 1));
        for (int s = 0; s <= numSlices; ++s)
            boundaries[static_cast<std::size_t>(s)] =
                static_cast<uint32_t>(static_cast<int64_t>(s) * totalFrames / numSlices);
    }

    std::vector<SampleZone> slices;
    for (int s = 0; s < numSlices; ++s) {
        const uint32_t f0 = boundaries[static_cast<std::size_t>(s)];
        const uint32_t f1 = boundaries[static_cast<std::size_t>(s + 1)];
        if (f1 <= f0) continue;

        SampleZone slice;
        slice.channelCount = src.channelCount;
        slice.sampleRate   = src.sampleRate;
        const std::size_t start = static_cast<std::size_t>(f0) * src.channelCount;
        const std::size_t end   = static_cast<std::size_t>(f1) * src.channelCount;
        slice.data.assign(src.data.begin() + static_cast<std::ptrdiff_t>(start),
                          src.data.begin() + static_cast<std::ptrdiff_t>(end));

        char name[64];
        std::snprintf(name, sizeof(name), "slice_%d", s);
        slice.sourcePath = name;
        slices.push_back(std::move(slice));
    }
    return slices;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <loop.wav> [num_slices]\n", argv[0]);
        return 1;
    }

    const std::string path = argv[1];
    const int numSlices = (argc >= 3) ? std::atoi(argv[2]) : 0;

    auto result = loadWavZone(path);
    if (!result.error.empty()) {
        std::fprintf(stderr, "Load error: %s\n", result.error.c_str());
        return 1;
    }
    result.zone.sourcePath = path;

    const int totalFrames = static_cast<int>(result.zone.data.size() / result.zone.channelCount);
    std::fprintf(stderr, "Loaded: %d frames, %d ch, %.0f Hz, %.2fs\n",
                 totalFrames, result.zone.channelCount, result.zone.sampleRate,
                 static_cast<double>(totalFrames) / result.zone.sampleRate);

    const auto slices = sliceZone(result.zone, numSlices);
    std::fprintf(stderr, "%d slices produced\n", static_cast<int>(slices.size()));

    // Print per-slice descriptor + score breakdown
    const std::string report = drumMapReport(slices);
    std::puts(report.c_str());

    // Print final assignment
    const auto assignments = assignDrumNotes(slices);
    std::puts("\n── Final assignments ──");
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        const auto& a = assignments[i];
        if (a.gmNote < 0)
            std::printf("  slice_%zu → NO MATCH (conf=%.3f)\n", i, a.confidence);
        else
            std::printf("  slice_%zu → MIDI %d (%s) conf=%.3f\n",
                        i, a.gmNote, a.evidence.c_str(), a.confidence);
    }
    return 0;
}
