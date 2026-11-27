#pragma once

#include "padseq_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::padseq {

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;       // zero-based
    double barBeat = 0.0;   // zero-based, including tick fraction
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 256> data {};
};

inline constexpr std::size_t kMaxMidiEvents = 512;

struct ProcessResult {
    std::array<MidiMessage, kMaxMidiEvents> events {};
    std::uint32_t eventCount = 0;
};

class Processor {
public:
    void init(double sampleRate);
    // Host lifecycle: clear timing, queued note-offs and LED caches, but never
    // reset the patch or the pattern. Hosts call this after restoring state.
    void activate();
    void setSampleRate(double sampleRate);

    void setParameter(std::uint32_t index, float value);
    [[nodiscard]] float getParameter(std::uint32_t index) const noexcept;

    [[nodiscard]] bool cellActive(std::size_t step, std::size_t note) const noexcept;
    [[nodiscard]] std::uint8_t noteForRow(std::size_t note) const noexcept;
    [[nodiscard]] int patternStepFor(std::size_t page, std::size_t stepColumn) const noexcept;

    ProcessResult processBlock(std::uint32_t frameCount,
                               const TransportSnapshot& transport,
                               const MidiMessage* inputEvents,
                               std::uint32_t inputEventCount);

private:
    struct PendingNoteOff {
        bool active = false;
        std::int64_t offStep = 0;
        std::uint8_t status = 0;
        std::uint8_t note = 0;
    };

    void resetToDefaults();
    void resetRuntime();
    void clampPatch();
    void updateStatus();
    void setCell(std::size_t index, bool active);
    void randomisePattern();
    void clearPattern(bool emitPanic);
    void releaseLedReset();

    bool handleMidi(const MidiMessage& event,
                    ProcessResult& result,
                    std::int64_t offStep,
                    bool& forceLeds);
    bool handleGridPress(std::uint8_t note);
    void handleTopButton(std::uint32_t button);
    void handleSideButton(std::size_t note,
                          ProcessResult& result,
                          std::uint32_t frame,
                          std::int64_t offStep);

    void prepareHostBlock(ProcessResult& result,
                          double absStepsStart,
                          int steps,
                          bool playing);
    void scheduleHostBoundaries(ProcessResult& result,
                                std::uint32_t frameCount,
                                double absStepsStart,
                                double absStepsEnd);
    void prepareFreeBlock(ProcessResult& result, double stepSamples);
    void scheduleFreeBoundaries(ProcessResult& result,
                                std::uint32_t frameCount,
                                double stepSamples);
    void scheduleIdleBoundaries(ProcessResult& result,
                                std::uint32_t frameCount,
                                double stepSamples);
    [[nodiscard]] std::int64_t offStepForFrame(double absStepsStart,
                                               double absStepsSpan,
                                               std::uint32_t frameCount,
                                               std::uint32_t frame) const noexcept;
    void flushPendingTriggers(ProcessResult& result,
                              std::uint32_t frame,
                              std::int64_t offStep);
    void emitStep(ProcessResult& result,
                  std::uint32_t frame,
                  std::int64_t absoluteStep,
                  std::int64_t patternBaseStep);
    void flushNoteOffsAt(ProcessResult& result, std::int64_t upToStep, std::uint32_t frame);
    void flushAllNoteOffs(ProcessResult& result, std::uint32_t frame);
    void enqueueNoteOff(std::int64_t offStep, std::uint8_t status, std::uint8_t note);

    void emitLedFeedback(ProcessResult& result, bool force);
    void emitPanic(ProcessResult& result);
    void appendClearAllSysex(ProcessResult& result);
    void appendClearAllMidi(ProcessResult& result);
    void appendLedBulkSysex(ProcessResult& result,
                            const std::array<std::uint8_t, kGridCellCount>& grid,
                            const std::array<std::uint8_t, kGridWidth>& side,
                            const std::array<std::uint8_t, kTopButtonCCs.size()>& top);

    void appendMidi(ProcessResult& result,
                    std::uint32_t frame,
                    std::uint8_t status,
                    std::uint8_t data1,
                    std::uint8_t data2);
    void appendSysex(ProcessResult& result, const std::uint8_t* data, std::size_t size);
    static void sortEvents(ProcessResult& result);

    [[nodiscard]] std::uint32_t random();
    [[nodiscard]] std::uint8_t statusColor(std::size_t step, std::size_t note) const noexcept;
    [[nodiscard]] std::uint8_t topColor(std::uint32_t button) const noexcept;
    [[nodiscard]] std::uint8_t sideColor(std::size_t note) const noexcept;

    double sampleRate_ = 48000.0;
    std::array<float, kParameterCount> parameters_ {};
    std::array<bool, kCellCount> cells_ {};
    std::array<PendingNoteOff, 256> pendingNoteOffs_ {};

    bool usingHost_ = false;
    bool wasHostPlaying_ = false;
    bool freeStarted_ = false;
    std::int64_t lastHostStep_ = -1;
    std::int64_t patternBaseStep_ = 0;
    std::int64_t currentPatternStep_ = -1;
    std::uint64_t freeStep_ = 0;
    double freeStepAccum_ = 0.0;
    double currentHostStep_ = 0.0;
    std::uint32_t ledRefreshSamples_ = 0;
    std::uint64_t rngState_ = 0x8f73a2bdu;
    float previousRandomise_ = 0.0f;
    float previousClear_ = 0.0f;
    float previousSeed_ = 12345.0f;
    std::array<float, kGridWidth> previousTriggers_ {};
    std::uint8_t pendingTriggerMask_ = 0;
    bool panicPending_ = false;
    bool clockResetPending_ = false;
    bool ledInitialized_ = false;
    // Clear/Panic leaves every LED dark until the user edits, triggers or runs
    // the pattern again. This stops a periodic refresh from re-lighting stale
    // top/side buttons immediately after a reset.
    bool ledResetHold_ = false;
    std::array<std::uint8_t, kGridCellCount> lastCellLeds_ {};
    std::array<std::uint8_t, kTopButtonCCs.size()> lastTopLeds_ {};
    std::array<std::uint8_t, kGridWidth> lastSideLeds_ {};
};

} // namespace downspout::padseq
