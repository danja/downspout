#pragma once

#include "GremlinEngine.hpp"
#include "gremlin_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::gremlin {

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 4> data {};
};

struct Status {
    std::array<float, kEffectiveParamCount> effective {};
    std::uint32_t currentScene = static_cast<std::uint32_t>(SceneId::manual);
    float controllerActivity = 0.0f;
    bool soloHeld = false;
};

class Processor {
public:
    static constexpr std::uint32_t kMaxOutputMidiEvents = 64;

    void init(double sampleRate);
    void activate();
    void setSampleRate(double sampleRate);

    void setLiveParameter(LiveParamId id, float value);
    void setHiddenParameter(HiddenParamId id, float value);
    void setMacro(MacroId id, float value);
    void setMomentary(MomentaryId id, bool enabled);
    void setMasterTrim(float value);

    [[nodiscard]] float getLiveParameter(LiveParamId id) const noexcept;
    [[nodiscard]] float getHiddenParameter(HiddenParamId id) const noexcept;
    [[nodiscard]] float getMacro(MacroId id) const noexcept;
    [[nodiscard]] bool getMomentary(MomentaryId id) const noexcept;
    [[nodiscard]] float getMasterTrim() const noexcept;
    [[nodiscard]] const Status& getStatus() const noexcept;
    // Pitch of the source note including pitch bend, before the per-voice intervals.
    [[nodiscard]] float currentFrequency() const noexcept { return engine_.baseFrequency(); }

    void loadScene(SceneId scene);
    void triggerAction(ActionId action);
    void processBlock(float* outLeft,
                      float* outRight,
                      std::uint32_t frameCount,
                      const MidiMessage* midiEvents,
                      std::uint32_t midiEventCount,
                      MidiMessage* outputEvents = nullptr,
                      std::uint32_t* outputEventCount = nullptr,
                      std::uint32_t outputEventCapacity = 0);

private:
    void resetToDefaults();
    void resetRuntime();
    void applyLiveState();
    void pitchBend(int channel, int value14);
    bool handleBendRpn(int channel, std::uint8_t cc, std::uint8_t value);
    void resetBend();
    bool handleMidiMessage(const MidiMessage& message);
    bool handleControllerCC(std::uint8_t cc, std::uint8_t value);
    bool handleControllerButton(std::uint8_t note, bool pressed);
    void cycleMode(int delta);
    void cycleScene(int delta);
    void randomizeSource();
    void randomizeDelay();
    void panic();
    void emitLedFeedback(MidiMessage* outputEvents,
                         std::uint32_t* outputEventCount,
                         std::uint32_t outputEventCapacity,
                         std::uint32_t frame,
                         bool force);
    void appendLedNote(MidiMessage* outputEvents,
                       std::uint32_t* outputEventCount,
                       std::uint32_t outputEventCapacity,
                       std::uint32_t frame,
                       std::uint8_t note,
                       bool lit);
    void renderRange(float* outLeft, float* outRight, std::uint32_t beginFrame, std::uint32_t endFrame);
    std::uint32_t nextRandom();
    float randomUnit();
    float randomRange(float minValue, float maxValue);
    void reseedEngine();

    double sampleRate_ = 48000.0;
    flues::gremlin::GremlinEngine engine_ {48000.0f};
    std::array<float, kLiveParamCount> live_ {};
    std::array<float, kHiddenParamCount> hidden_ {};
    std::array<float, kMacroCount> macros_ {};
    std::array<bool, kMomentaryCount> momentary_ {};
    std::array<std::uint8_t, 8> lastMuteLeds_ {};
    std::array<std::uint8_t, 8> lastSoloMuteLeds_ {};
    std::array<std::uint8_t, 8> lastRecArmLeds_ {};
    std::array<std::uint8_t, 3> lastBankLeds_ {};
    Status status_ {};
    float masterTrim_ = 0.45f;
    std::uint32_t rngState_ = 0x4d3c2b1au;
    std::uint32_t ledRefreshSamples_ = 0;
    int currentNote_ = -1;
    int currentChannel_ = 0;
    std::array<float, 16> bendRatio_ {};
    std::array<float, 16> bendRange_ {};
    std::array<int, 16> bendValue_ {};
    std::array<std::uint8_t, 16> rpnMsb_ {};
    std::array<std::uint8_t, 16> rpnLsb_ {};
    float postGain_ = 1.0f;
    bool soloHeld_ = false;
    bool ledInitialized_ = false;
};

}  // namespace downspout::gremlin
