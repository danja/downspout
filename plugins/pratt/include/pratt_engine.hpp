#pragma once

// Real-time Pratt engine: polyphonic wavetable synth, Pratt filter effect, or
// both. Portable core; the DPF wrapper only forwards parameters, MIDI and audio.
//
// Differences from the offline Python renderer (all intentional, see docs):
//  * envelopes are gate-based (the plugin cannot know a note's length up front);
//    attack is not shortened for short notes and release decays to -80 dB;
//  * no per-file peak normalisation: a fixed master gain and a soft clip;
//  * voices are capped (kMaxVoices) and stolen, the Python had no limit;
//  * RPN bend range is not parsed; `bendRange` is a parameter;
//  * percussion gains are not normalised per hit and are unvalidated by ear.
//
// Threading: setters, MIDI and process() belong to the audio thread. Table and
// section construction is lazy and allocates; `warmCaches()`, `preload()` and
// `preloadDrums()` are safe to call from a worker thread while audio runs (the
// caches are mutex-guarded) and exist to keep that work off the audio thread.
// Lookups on the audio thread still take the cache locks briefly.

#include "pratt_poly.hpp"
#include "pratt_presets.hpp"
#include "pratt_wavetable.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

namespace downspout::pratt {

inline constexpr int kMaxVoices = 64;
inline constexpr int kMaxSections = 13;  // == max polynomial degree for n <= kMaxIndex

enum class Mode : std::uint8_t {
    Synth = 0,   // MIDI -> wavetable voices -> room
    Filter,      // audio input -> Pratt filter
    Both,        // (audio input + synth) -> Pratt filter
};

struct EngineParams {
    Mode mode = Mode::Synth;
    int presetOverride = -1;   // -1: follow each channel's GM program
    int baseOverride = 0;      // 0: use the preset's Pratt base, else 1..64
    double xiScale = 1.0;      // scales preset xi (brighter > 1)
    double rollOffset = 0.0;   // added to preset roll (darker > 0)
    double bendRange = 2.0;    // semitones
    bool percussion = true;    // MIDI channel 10 plays procedural drums
    double room = 1.0;         // 0..1 send level scale (1 = Python reference)
    double masterGain = 0.5;
    // Filter. Effective index n = indexA * indexB (H_mn = H_m H_n), clamped to kMaxIndex.
    int filterIndexA = 5;
    int filterIndexB = 7;
    double filterCutoffHz = 800.0;  // w0 / 2pi; clamped to 0.1 * sample rate
    double filterMix = 1.0;         // 0 dry .. 1 wet
};

// The parameters that shape a wavetable besides the preset itself.
struct VoiceTuning {
    int base = 0;           // 0: preset's Pratt base
    double xiScale = 1.0;
    double rollOffset = 0.0;
};

// Fixed-capacity biquad cascade so the audio path never allocates.
struct Cascade {
    std::array<Biquad, kMaxSections> sections{};
    int count = 0;
    // 1 / simulated peak of a noise-driven drum hit (set by the drum cache only).
    double noiseNorm = 1.0;
};

Cascade buildCascade(int n, double cutoffHz, double sampleRate);

// Wavetable cache key: everything buildWavetable depends on besides fs.
using TableKey = std::tuple<int /*pitch*/, int /*preset*/, int /*velBucket*/, bool /*dark*/,
                            int /*base*/, int /*xi x1000*/, int /*roll x1000*/>;

class Engine {
public:
    explicit Engine(double sampleRate);
    ~Engine();

    void setParams(const EngineParams& p);
    const EngineParams& params() const { return params_; }

    // Fill the polynomial/root caches for every index. ~0.3 s; call off the audio thread.
    static void warmCaches();
    // Same, for indices [first, last]; lets a worker interleave cancellation checks.
    static void warmRoots(int first, int last);
    // Drop cached tables whose tuning differs from `keep`, so dragging a tuning
    // control does not accumulate stale tables. Frees outside the lock. Worker thread only.
    void trimTables(const VoiceTuning& keep);
    // Build tables for one preset over a pitch range (all 8 velocity buckets). Thread-safe.
    void preload(int preset, const VoiceTuning& tuning, int lowPitch = 24, int highPitch = 96);
    // Build the drum filters for the General MIDI percussion range. Thread-safe.
    void preloadDrums();
    // One drum filter (GM percussion note); lets a worker interleave cancellation checks.
    void preloadDrum(int pitch);

    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void controlChange(int channel, int cc, int value);
    void pitchBend(int channel, int value14);  // 0..16383, 8192 = centre
    void programChange(int channel, int program);
    void allNotesOff();
    void allSoundOff();
    // Raw 1-3 byte MIDI message. Returns true if recognised.
    bool handleMidi(const std::uint8_t* data, int size);

    // inL/inR may be null (treated as silence).
    void process(const float* inL, const float* inR, float* outL, float* outR, int frames);

    int activeVoices() const;

private:
    struct Voice;
    struct Channel;
    struct FilterBank;

    std::shared_ptr<const Wavetable> table(int pitch, int preset, int bucket, bool dark, const VoiceTuning& tuning);
    Cascade drumCascade(int pitch);
    Voice* allocateVoice();
    void renderVoice(Voice& v, float& l, float& r);

    double fs_;
    EngineParams params_;
    std::mutex tablesMutex_, drumMutex_;
    std::map<TableKey, std::shared_ptr<const Wavetable>> tables_;
    std::map<int, Cascade> drumCascades_;
    std::uint64_t noteCounter_ = 0;

    std::vector<Voice> voices_;
    std::vector<Channel> channels_;
    std::unique_ptr<FilterBank> filter_;

    // Room and DC handling.
    std::vector<float> roomBuffer_;
    std::size_t roomPos_ = 0;
    double roomLpState_ = 0.0, roomLpCoef_ = 0.0;
    double hpCoef_ = 0.0, hpPrevInL_ = 0.0, hpPrevInR_ = 0.0, hpPrevOutL_ = 0.0, hpPrevOutR_ = 0.0;
    std::vector<std::array<int, 3>> roomTaps_;  // samples, level x1e6, side
};

}  // namespace downspout::pratt
