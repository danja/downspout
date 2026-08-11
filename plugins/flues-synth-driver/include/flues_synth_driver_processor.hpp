#pragma once

#include "flues_synth_driver_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::flues_synth_driver {

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t  size  = 0;
    std::array<std::uint8_t, 4> data {};
};

inline constexpr std::size_t kMaxOutputEvents = 512;

struct ProcessResult {
    std::array<MidiMessage, kMaxOutputEvents> events {};
    std::uint32_t eventCount  = 0;
    float midiActivity  = 0.0f;
    bool  wasRandomized = false;
    bool  wasPanicked   = false;
};

class Processor {
public:
    void init(double sampleRate);
    void activate();

    // Setters called from DPF wrapper (audio thread)
    void setAlgorithm(int v);
    void setDisynP1(float v);
    void setDisynP2(float v);
    void setDisynP3(float v);
    void setNoiseLevel(float v);
    void setDcLevel(float v);
    void setInterfaceType(int v);
    void setIntensity(float v);
    void setF1(float v);
    void setF2(float v);
    void setF3(float v);
    void setF4(float v);
    void setNasal(bool v);
    void setSing(bool v);
    void setShout(bool v);
    void setFry(bool v);
    void setAttack(float v);
    void setRelease(float v);
    void setTuning(float v);
    void setDelay1Fb(float v);
    void setDelay2Fb(float v);
    void setDelayRatio(float v);
    void setFilterFb(float v);
    void setFilterFreq(float v);
    void setFilterQ(float v);
    void setFilterShape(float v);
    void setLfoFreq(float v);
    void setAmFmDepth(float v);
    void setGain(float v);
    void setTrajSides(int v);
    void setTrajStartPos(float v);
    void setTrajStartAngle(float v);
    void setTrajJitter(float v);
    void setTrajClip(float v);
    void setTrajMixX(float v);
    void setTrajMixY(float v);
    void setProgram(int prog);        // 0-30
    void setOutputChannel(int ch);   // 1-16
    void setConductorCh(int ch);     // 0=off, 1-16
    void setPassInput(bool v);
    void triggerPanic();
    void triggerRandomize();

    // Getters
    [[nodiscard]] float getSynthParam(std::size_t paramIndex) const noexcept;

    ProcessResult processBlock(std::uint32_t frameCount,
                               const MidiMessage* inputEvents,
                               std::uint32_t inputEventCount);

private:
    void markDirty(std::size_t synthParamIndex);
    void emitCC(ProcessResult& result, std::uint32_t frame,
                std::uint8_t ch, std::uint8_t cc, std::uint8_t value);
    void emitAllNotesOff(ProcessResult& result, std::uint32_t frame);
    void emitSliderCCs(ProcessResult& result, std::uint32_t frame);
    void emitDefaultCCs(ProcessResult& result, std::uint32_t frame);
    [[nodiscard]] std::uint8_t computeCC(std::size_t paramIndex) const noexcept;
    void handleConductorCC(ProcessResult& result, std::uint32_t frame,
                           std::uint8_t cc, std::uint8_t value);

    // Synth param storage (indexed by Param enum - kParamAlgorithm)
    std::array<float, kSynthParamCount> params_ {};

    // Dirty flags: one per synth param
    std::array<bool, kSynthParamCount> dirty_ {};

    // Last CC values sent (to suppress redundant CCs)
    std::array<int, kSynthParamCount> lastCC_ {};

    int outputChannel_     = 1;     // 1-16
    int conductorCh_       = 0;     // 0=off
    int program_           = 0;     // 0-30
    bool passInput_        = true;
    bool panicPending_     = false;
    bool programPending_   = false;
    bool randomizePending_ = false;
    float midiActivity_    = 0.0f;
    double sampleRate_     = 44100.0;
    std::uint32_t randomState_ = 0x3c6ef372u;
};

}  // namespace downspout::flues_synth_driver
