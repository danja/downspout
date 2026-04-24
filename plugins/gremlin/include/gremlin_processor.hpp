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
};

class Processor {
public:
    void init(double sampleRate);
    void activate();

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

    void loadScene(SceneId scene);
    void triggerAction(ActionId action);
    void processBlock(float* outLeft,
                      float* outRight,
                      std::uint32_t frameCount,
                      const MidiMessage* midiEvents,
                      std::uint32_t midiEventCount);

private:
    void resetToDefaults();
    void applyLiveState();
    bool handleMidiMessage(const MidiMessage& message);
    bool handleControllerCC(std::uint8_t cc, std::uint8_t value);
    bool handleControllerButton(std::uint8_t note, bool pressed);
    void cycleMode(int delta);
    void cycleScene(int delta);
    void randomizeSource();
    void randomizeDelay();
    void panic();
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
    Status status_ {};
    float masterTrim_ = 0.45f;
    std::uint32_t rngState_ = 0x4d3c2b1au;
    int currentNote_ = -1;
    float postGain_ = 1.0f;
};

}  // namespace downspout::gremlin
