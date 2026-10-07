#pragma once

#include "paunchlad_params.hpp"

#include <array>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

namespace downspout::paunchlad {

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 16> data {};
};

struct Status {
    float activity = 0.0f;
    float mode = 0.0f;
};

inline constexpr std::size_t kMaxProcessEvents = 256;

struct ProcessResult {
    std::array<MidiMessage, kMaxProcessEvents> events {};
    std::uint32_t eventCount = 0;
    Status status {};
};

enum class LaunchpadInputLayout {
    unknown,
    programmer,
    custom,
};

class Processor {
public:
    // First-time setup: sets the rate, allocates the delay and puts every
    // parameter at its default.
    void init(double sampleRate);
    // The host started or resumed processing. Clears sounding state and the LED
    // cache but keeps the settings: hosts call this after restoring state, so
    // resetting parameters here discarded the user's mix.
    void activate();
    // The host changed the sample rate. Resizes the delay, keeps the settings.
    void setSampleRate(double sampleRate);

    // Host session state: the nine settings as a versioned, symbol-keyed text
    // form. Pad triggers and the panic trigger are momentary and the status
    // outputs are derived, so none are stored. A corrupt, truncated or
    // future-version value is rejected rather than half-applied.
    [[nodiscard]] std::string serializeParameters() const;
    [[nodiscard]] bool deserializeParameters(std::string_view text);

    void setParameter(std::uint32_t index, float value);
    [[nodiscard]] float getParameter(std::uint32_t index) const noexcept;
    [[nodiscard]] const Status& getStatus() const noexcept;

    ProcessResult processBlock(const float* inLeft,
                               const float* inRight,
                               float* outLeft,
                               float* outRight,
                               std::uint32_t frameCount,
                               const MidiMessage* inputEvents,
                               std::uint32_t inputEventCount);

private:
    void resetToDefaults();
    void allocateDelay();
    void clearPerformance();
    bool handleMidi(const MidiMessage& message);
    bool handleGridPress(std::uint8_t note, std::uint8_t velocity);
    bool handleTopButton(std::uint8_t cc);
    bool handleSideButton(std::uint8_t cc);
    void triggerPad(std::size_t row, std::size_t col, float strength);
    void processAudio(const float* inLeft,
                      const float* inRight,
                      float* outLeft,
                      float* outRight,
                      std::uint32_t frameCount);
    void emitLedFeedback(ProcessResult& result, bool force);
    void appendMidi(ProcessResult& result,
                    std::uint32_t frame,
                    std::uint8_t status,
                    std::uint8_t data1,
                    std::uint8_t data2);
    void appendSysex(ProcessResult& result, const std::uint8_t* data, std::uint8_t size);
    [[nodiscard]] std::uint8_t colorForPad(std::size_t row, std::size_t col, bool active) const noexcept;
    [[nodiscard]] std::uint32_t framesForBeatDivision(float beats) const noexcept;
    [[nodiscard]] std::uint32_t random();
    [[nodiscard]] float noise();
    [[nodiscard]] float parameterDefault(std::uint32_t index) const noexcept;
    void decayCellFlashes(std::uint32_t frameCount);

    double sampleRate_ = 48000.0;
    std::array<float, kParameterCount> parameters_ {};
    std::array<float, kCellCount> cellFlash_ {};
    std::array<std::uint8_t, kCellCount> lastCellLeds_ {};
    std::array<std::uint8_t, 9> lastTopLeds_ {};
    std::array<std::uint8_t, 8> lastSideLeds_ {};
    std::vector<float> delayLeft_ {};
    std::vector<float> delayRight_ {};
    std::size_t delayWrite_ = 0;
    float delaySeconds_ = 0.34f;
    float delayToneLeft_ = 0.0f;
    float delayToneRight_ = 0.0f;
    float sirenPhase_ = 0.0f;
    float sirenLfo_ = 0.0f;
    float sirenEnv_ = 0.0f;
    float sirenSweep_ = 0.0f;
    int sirenMode_ = 0;
    float alarmPhase_ = 0.0f;
    float alarmEnv_ = 0.0f;
    float alarmRate_ = 3.5f;
    int alarmMode_ = 0;
    float snareEnv_ = 0.0f;
    float snareBodyPhase_ = 0.0f;
    float snareBodyHz_ = 180.0f;
    float crashEnv_ = 0.0f;
    float subEnv_ = 0.0f;
    float subPhase_ = 0.0f;
    float subHz_ = 74.0f;
    float laserEnv_ = 0.0f;
    float laserPhase_ = 0.0f;
    float laserStartHz_ = 1200.0f;
    float laserEndHz_ = 160.0f;
    float thunderEnv_ = 0.0f;
    float monsterEnv_ = 0.0f;
    float monsterPhase_ = 0.0f;
    float zapEnv_ = 0.0f;
    float zapPhase_ = 0.0f;
    float rewindEnv_ = 0.0f;
    float rewindPhase_ = 0.0f;
    float bubbleEnv_ = 0.0f;
    float bubblePhase_ = 0.0f;
    float riserEnv_ = 0.0f;
    float riserPhase_ = 0.0f;
    float hornEnv_ = 0.0f;
    float hornPhase_ = 0.0f;
    int effectMode_ = 0;
    float springEnv_ = 0.0f;
    float springState_ = 0.0f;
    float throwSend_ = 0.0f;
    float throwFeedback_ = 0.0f;
    std::uint32_t throwFrames_ = 0;
    std::uint32_t dropFrames_ = 0;
    std::uint32_t chopFrames_ = 0;
    std::uint32_t freezeFrames_ = 0;
    std::uint32_t ledRefreshSamples_ = 0;
    std::uint32_t randomState_ = 0x61c88647u;
    bool ledInitialized_ = false;
    LaunchpadInputLayout inputLayout_ = LaunchpadInputLayout::unknown;
    Status status_ {};
};

} // namespace downspout::paunchlad
