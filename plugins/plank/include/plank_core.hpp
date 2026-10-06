#pragma once

#include "plank_params.hpp"
#include "plank_voice.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace downspout::plank {

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint16_t size = 0;
    std::array<std::uint8_t, 256> data {};
};

struct Status {
    float stringLevels[kStringCount] {};  // per-string envelope followers
    float activeStrings = 0.0f;
    float peak = 0.0f;
};

inline constexpr std::size_t kMaxProcessEvents = 768;

struct ProcessResult {
    std::array<MidiMessage, kMaxProcessEvents> events {};
    std::uint32_t eventCount = 0;
    Status status {};
};

// Portable core: no DPF, no host API, no framework types. Deterministic for a
// given parameter set, MIDI input and transport.
//
// Grid events are quantised to the processing block, matching how the reference
// plugins in this repository drive their cores. With a typical 64 to 512 frame
// block that is at most a few milliseconds of quantisation, well under the 8 ms
// default attack.
class Processor {
public:
    // First-time setup: sets the rate and puts every parameter at its default.
    void init(double sampleRate);
    // The host started or resumed processing. Clears voices, LFOs and effect
    // tails but keeps the patch: hosts call this after restoring parameters, so
    // resetting them here discarded the user's settings.
    void activate();
    // The host changed the sample rate. Keeps the patch, rebuilds the effect
    // buffers and clears running state.
    void setSampleRate(double sampleRate);

    void setParameter(std::uint32_t index, float value);
    [[nodiscard]] float getParameter(std::uint32_t index) const noexcept;
    [[nodiscard]] const Status& getStatus() const noexcept;

    // Renders into caller-provided stereo buffers and reports outgoing MIDI
    // (Launchpad LED feedback, plus optional note passthrough) via `result`.
    void process(float* left,
                 float* right,
                 std::uint32_t frameCount,
                 const TransportSnapshot& transport,
                 const MidiMessage* inputEvents,
                 std::uint32_t inputEventCount,
                 ProcessResult& result);

    // Grid mapping helpers, shared with the UI and the tests.
    [[nodiscard]] std::uint8_t noteForCell(std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] int scaleStep(int degree) const noexcept;
    [[nodiscard]] int scaleInterval(std::size_t degree) const noexcept;
    [[nodiscard]] float levelForString(std::size_t index) const noexcept;
    [[nodiscard]] std::uint8_t ledColorForCell(std::size_t row, std::size_t col) const noexcept;

    // Host session state. A versioned, symbol-keyed text form, so parameters
    // can be reordered or inserted without invalidating saved projects, and a
    // corrupt or truncated value is rejected rather than half-applied.
    [[nodiscard]] std::string serializeParameters() const;
    [[nodiscard]] bool deserializeParameters(std::string_view text);

private:
    [[nodiscard]] std::optional<std::array<float, kParameterCount>> parseState(std::string_view text) const;

public:

private:
    struct Modulation {
        float pitch = 0.0f;   // semitones
        float cutoff = 1.0f;  // multiplier
        float morph = 0.0f;   // additive
        float noise = 0.0f;   // additive
        float drive = 1.0f;   // multiplier
    };

    static constexpr std::size_t kDelayMaxSeconds = 2;
    static constexpr std::size_t kReverbLines = kReverbLineCount;
    static constexpr std::size_t kReverbMaxSeconds = 4;

    // ── Grid / string control ──
    void pressGrid(std::size_t row, std::size_t col, ProcessResult& result, std::uint32_t frame);
    void releaseGrid(std::size_t row, std::size_t col, ProcessResult& result, std::uint32_t frame);
    void releaseAllStrings(ProcessResult& result, std::uint32_t frame);
    void startNote(Voice& voice, std::size_t col, std::size_t row, ProcessResult& result, std::uint32_t frame);
    void stopNote(Voice& voice, std::size_t col, ProcessResult& result, std::uint32_t frame, bool immediate);
    void setDegree(Voice& voice, std::size_t row, std::size_t col);

    // ── MIDI input ──
    bool handleMidi(const MidiMessage& event, ProcessResult& result);
    bool handleTopButton(std::uint8_t cc, ProcessResult& result, std::uint32_t frame);
    bool handleSideButton(std::uint8_t cc);

    // ── Rendering ──
    void renderVoice(std::size_t index,
                     float* left,
                     float* right,
                     std::uint32_t frameCount,
                     const Modulation& modulation);
    void renderDelay(float* left, float* right, std::uint32_t frameCount, const TransportSnapshot& transport);
    void renderReverb(float* left, float* right, std::uint32_t frameCount);
    void applyOutputStage(float* left, float* right, std::uint32_t frameCount);
    void resetEffects();

    // ── Modulation ──
    void advanceLfos(std::uint32_t frameCount);
    [[nodiscard]] float lfoSample(Lfo& lfo, std::size_t shape) noexcept;
    [[nodiscard]] Modulation computeModulation() const noexcept;
    [[nodiscard]] std::uint32_t oscillatorIncrement(float semitones) const noexcept;
    [[nodiscard]] float envelopeStep(float milliseconds) const noexcept;
    [[nodiscard]] std::uint8_t midiNoteFor(const Voice& voice) const noexcept;
    [[nodiscard]] std::uint8_t baseChannel() const noexcept;

    // ── Launchpad LED feedback ──
    void emitLedFeedback(ProcessResult& result, bool force);
    void requestPanic();
    void runPanic(ProcessResult& result);

    // Scratch buffer for MIDI produced by out-of-band parameter changes.
    ProcessResult scratch_ {};

    // ── MIDI plumbing ──
    void appendMidi(ProcessResult& result,
                    std::uint32_t frame,
                    std::uint8_t status,
                    std::uint8_t data1,
                    std::uint8_t data2);
    void appendSysex(ProcessResult& result, const std::uint8_t* data, std::uint16_t size);
    void appendClearAllSysex(ProcessResult& result);
    void appendClearAllMidi(ProcessResult& result);

    // Parameter changes can arrive between blocks (the UI thread, or host
    // automation), so any MIDI they generate is queued here and flushed at the
    // start of the next process() call rather than being discarded.
    void queueMidi(std::uint8_t status, std::uint8_t data1, std::uint8_t data2);
    void flushPendingMidi(ProcessResult& result);

    [[nodiscard]] std::uint32_t random();
    void updateStatus(float peak);
    void resetToDefaults();
    void resetRuntime();

    double sampleRate_ = 48000.0;
    std::array<float, kParameterCount> parameters_ {};
    std::array<Voice, kStringCount> voices_ {};
    std::array<Lfo, 2> lfos_ {};

    // Delay line, stereo.
    std::vector<float> delayLeft_;
    std::vector<float> delayRight_;
    std::size_t delayPosition_ = 0;
    float delaySamples_ = 0.0f;
    float delayWobblePhase_ = 0.0f;

    // Reverb: four comb lines with a damped, shimmer-modulated feedback path.
    std::array<std::vector<float>, kReverbLines> reverbLines_ {};
    std::array<std::size_t, kReverbLines> reverbPositions_ {};
    float reverbDamp_ = 0.0f;
    float reverbWobblePhase_ = 0.0f;

    std::uint32_t ledRefreshSamples_ = 0;
    std::uint32_t randomState_ = 0x1f123bb5u;
    std::array<std::uint8_t, kCellCount> lastCellLeds_ {};
    std::array<std::uint8_t, kTopButtonCCs.size()> lastTopLeds_ {};
    std::array<std::uint8_t, kSideButtonCCs.size()> lastSideLeds_ {};

    // MIDI generated outside process(), i.e. by parameter changes.
    std::array<MidiMessage, 64> pendingMidi_ {};
    std::uint32_t pendingMidiCount_ = 0;

    bool ledInitialized_ = false;
    bool panicRequested_ = false;
    bool suppressLedFeedbackOnce_ = false;
    Status status_ {};
};

}  // namespace downspout::plank
