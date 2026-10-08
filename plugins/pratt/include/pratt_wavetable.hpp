#pragma once

// Band-limited single-cycle tables holding the stationary Pratt-filtered
// harmonic spectrum, ported from `wavetable()` in pratt_midi_synth.py.

#include <cstdint>
#include <vector>

namespace downspout::pratt {

struct TableParams {
    int pitch = 69;          // MIDI key; f0 = 440 * 2^((pitch-69)/12)
    int base = 5;            // preset Pratt base; index n = (pitch + 1) * base
    int timbre = 1;          // extra module chain H_timbre, same for every pitch; 1 = none (skipped)
    double roll = 0.92;      // excitation a_k = k^-(roll + extraRoll)
    double xi = 0.15;        // filter scale: R(k) = H_n(i * xi_eff * k)
    double velocity = 0.5;   // 0..1; xi_eff = xi * (1.20 - 0.30 * velocity)
    double extraRoll = 0.0;  // the Python "dark" variant uses 0.75
    double evenGain = 1.0;   // reed: 0.32
    int upperStart = 0;      // harmonics k > upperStart get upperGain (flute: 3)
    double upperGain = 1.0;  // flute: 0.35
    double sampleRate = 44100.0;
    int tableSize = 4096;
    int maxHarmonics = 56;
};

struct Wavetable {
    std::vector<float> samples;  // tableSize + 1 (guard point == samples[0]), peak-normalised to 1
    int index = 0;               // Pratt index n = (pitch + 1) * base
    int timbre = 1;              // cascaded module chain index
    int harmonics = 0;           // partials kept below 0.42 * fs
};

double pitchToHz(int pitch);

Wavetable buildWavetable(const TableParams& params);

// Linear-interpolated read; phase in cycles (any real value).
float readWavetable(const std::vector<float>& samples, double phase);

}  // namespace downspout::pratt
