#pragma once

#include "lifeform_params.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace downspout::lifeform {

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
    float activeCells = 0.0f;
    float generation = 0.0f;
    float births = 0.0f;
};

inline constexpr std::size_t kMaxProcessEvents = 768;

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
    // (randomize, clear, step, panic) and the status outputs are not stored. A
    // corrupt, truncated or future-version value is rejected rather than
    // half-applied, leaving the current patch untouched.
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

    void resetToDefaults();
    void resetRuntime();
    void clearCells();
    void randomizeCells();
    void seedPattern();
    void setCell(std::size_t row, std::size_t col, bool alive);
    bool handleMidi(const MidiMessage& event, ProcessResult& result);
    bool handleGridPress(std::uint8_t note);
    bool handleTopButton(std::uint8_t cc, ProcessResult& result, std::uint32_t frame);
    bool handleSideButton(std::uint8_t cc);
    void processPendingNoteOffs(ProcessResult& result, std::uint32_t frameCount);
    void scheduleClockedGenerations(std::array<std::uint32_t, 32>& frames,
                                    std::uint32_t& count,
                                    std::uint32_t frameCount,
                                    const TransportSnapshot& transport);
    void scheduleFreeGenerations(std::array<std::uint32_t, 32>& frames,
                                 std::uint32_t& count,
                                 std::uint32_t frameCount,
                                 double bpm);
    void runGeneration(ProcessResult& result, std::uint32_t frame);
    void emitCurrentCells(ProcessResult& result, std::uint32_t frame);
    void emitCell(ProcessResult& result,
                  std::uint32_t frame,
                  std::size_t row,
                  std::size_t col,
                  bool born);
    void emitNote(ProcessResult& result,
                  std::uint32_t frame,
                  std::uint8_t channel,
                  std::uint8_t note,
                  std::uint8_t velocity,
                  std::uint32_t gateFrames);
    void appendNoteOff(std::uint8_t status, std::uint8_t note, std::uint32_t framesLeft);
    void requestPanic();
    void runPanic(ProcessResult& result);
    void emitLedFeedback(ProcessResult& result, bool force);
    void appendMidi(ProcessResult& result,
                    std::uint32_t frame,
                    std::uint8_t status,
                    std::uint8_t data1,
                    std::uint8_t data2);
    void appendSysex(ProcessResult& result, const std::uint8_t* data, std::uint16_t size);
    void appendClearAllSysex(ProcessResult& result);
    void appendClearAllMidi(ProcessResult& result);
    [[nodiscard]] int liveNeighborCount(std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] std::uint8_t outputChannel(std::size_t row) const noexcept;
    [[nodiscard]] std::uint8_t noteForCell(std::size_t row, std::size_t col) const noexcept;
    [[nodiscard]] std::uint8_t velocityForCell(std::size_t row, std::size_t col, bool born) const noexcept;
    [[nodiscard]] std::uint8_t ledColorForCell(std::size_t row, std::size_t col, bool alive) const noexcept;
    [[nodiscard]] float parameterDefault(std::uint32_t index) const noexcept;
    [[nodiscard]] std::uint32_t random();
    [[nodiscard]] int scaleDegreeToMidi(int degree, int octaveShift) const noexcept;
    void updateStatus();

    double sampleRate_ = 48000.0;
    std::array<float, kParameterCount> parameters_ {};
    std::array<bool, kCellCount> cells_ {};
    std::array<bool, kCellCount> born_ {};
    std::array<std::uint8_t, kCellCount> lastCellLeds_ {};
    std::array<std::uint8_t, kTopButtonCCs.size()> lastTopLeds_ {};
    std::array<std::uint8_t, 8> lastSideLeds_ {};
    std::array<PendingNoteOff, 192> pendingNoteOffs_ {};
    std::uint64_t generation_ = 0;
    std::uint64_t lastHostBeat_ = static_cast<std::uint64_t>(-1);
    double freeBeatSamples_ = 0.0;
    std::uint32_t ledRefreshSamples_ = 0;
    std::uint32_t randomState_ = 0x1f123bb5u;
    bool ledInitialized_ = false;
    bool panicRequested_ = false;
    bool suppressLedFeedbackOnce_ = false;
    Status status_ {};
};

} // namespace downspout::lifeform
