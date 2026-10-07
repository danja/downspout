#pragma once

#include "luma_params.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace downspout::luma {

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
    std::uint8_t size = 0;
    std::array<std::uint8_t, 16> data {};
};

struct Status {
    float activeCells = 0.0f;
    float currentStep = 0.0f;
    float ledActivity = 0.0f;
};

inline constexpr std::size_t kMaxProcessEvents = 512;

struct ProcessResult {
    std::array<MidiMessage, kMaxProcessEvents> events {};
    std::uint32_t eventCount = 0;
    Status status {};
};

class Processor {
public:
    // First-time setup: sets the rate and puts every parameter and the cell
    // pattern at its default.
    void init(double sampleRate);
    // The host started or resumed processing. Clears timing, pending notes and the
    // LED cache but keeps the patch: hosts call this after restoring state, so
    // resetting parameters here discarded the user's settings and pattern.
    void activate();
    // The host changed the sample rate. Keeps the patch.
    void setSampleRate(double sampleRate);

    // Host session state: the settings plus the live cell pattern as a 64
    // character bitmap, in a versioned, symbol-keyed text form. Triggers
    // (randomize, clear) and the status outputs are not stored. A corrupt,
    // truncated or future-version value is rejected rather than half-applied.
    [[nodiscard]] std::string serializeParameters() const;
    [[nodiscard]] bool deserializeParameters(std::string_view text);

    void setParameter(std::uint32_t index, float value);
    [[nodiscard]] float getParameter(std::uint32_t index) const noexcept;
    [[nodiscard]] const Status& getStatus() const noexcept;

    ProcessResult processBlock(std::uint32_t frameCount,
                               const TransportSnapshot& transport,
                               const MidiMessage* inputEvents,
                               std::uint32_t inputEventCount);

private:
    struct PendingNoteOff {
        bool active = false;
        std::uint8_t status = 0;
        std::uint8_t note = 0;
        std::uint32_t framesLeft = 0;
    };

    enum class AgentRole {
        bass,
        chord,
        melody,
        drum,
    };

    void resetToDefaults();
    void resetRuntime();
    void clearCells();
    void randomizeCells();
    bool handleMidi(const MidiMessage& event, ProcessResult& result);
    bool handleGridPress(std::uint8_t note);
    bool handleTopButton(std::uint8_t cc);
    bool handleSideButton(std::uint8_t cc);
    void processPendingNoteOffs(ProcessResult& result, std::uint32_t frameCount);
    void scheduleClockedSteps(ProcessResult& result,
                              std::uint32_t frameCount,
                              const TransportSnapshot& transport);
    void scheduleFreeSteps(ProcessResult& result, std::uint32_t frameCount, double bpm);
    void emitStep(ProcessResult& result, std::uint32_t frame, std::uint64_t step);
    void emitCell(ProcessResult& result,
                  std::uint32_t frame,
                  std::uint64_t step,
                  std::size_t row,
                  std::size_t col);
    void emitNote(ProcessResult& result,
                  std::uint32_t frame,
                  std::uint8_t channel,
                  std::uint8_t note,
                  std::uint8_t velocity,
                  std::uint32_t gateFrames);
    void appendNoteOff(std::uint8_t status, std::uint8_t note, std::uint32_t framesLeft);
    void emitLedFeedback(ProcessResult& result, bool force);
    void appendMidi(ProcessResult& result,
                    std::uint32_t frame,
                    std::uint8_t status,
                    std::uint8_t data1,
                    std::uint8_t data2);
    void appendSysex(ProcessResult& result, const std::uint8_t* data, std::uint8_t size);
    [[nodiscard]] AgentRole roleForRow(std::size_t row) const noexcept;
    [[nodiscard]] bool shouldFire(std::uint64_t step, std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] std::uint8_t outputChannel(AgentRole role) const noexcept;
    [[nodiscard]] std::uint8_t noteForCell(AgentRole role, std::size_t row, std::size_t col, int chordOffset) const noexcept;
    [[nodiscard]] std::uint8_t velocityForCell(AgentRole role, std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] std::uint8_t ledColorForCell(std::size_t row, std::size_t col, bool active) const noexcept;
    [[nodiscard]] float parameterDefault(std::uint32_t index) const noexcept;
    [[nodiscard]] std::uint32_t random();
    [[nodiscard]] float chanceForCell(std::uint64_t step, std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] int scaleDegreeToMidi(int degree, int octaveShift) const noexcept;
    void updateStatus();

    double sampleRate_ = 48000.0;
    std::array<float, kParameterCount> parameters_ {};
    std::array<bool, kCellCount> cells_ {};
    std::array<std::uint8_t, kCellCount> lastCellLeds_ {};
    std::array<std::uint8_t, 9> lastTopLeds_ {};
    std::array<std::uint8_t, 8> lastSideLeds_ {};
    std::array<PendingNoteOff, 128> pendingNoteOffs_ {};
    std::uint64_t lastHostStep_ = static_cast<std::uint64_t>(-1);
    std::uint64_t freeStep_ = 0;
    double freeStepSamples_ = 0.0;
    std::uint32_t ledRefreshSamples_ = 0;
    std::uint32_t randomState_ = 0x8f73a2bdu;
    bool ledInitialized_ = false;
    Status status_ {};
};

} // namespace downspout::luma
