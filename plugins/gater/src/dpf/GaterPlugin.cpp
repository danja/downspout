#include "DistrhoPlugin.hpp"
#include "gater_engine.hpp"

START_NAMESPACE_DISTRHO

class GaterPlugin : public Plugin
{
public:
    GaterPlugin() : Plugin(0, 0, 0) {}

protected:
    const char* getLabel() const override { return "Gater"; }
    const char* getDescription() const override { return "MIDI-controlled audio switcher."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('G', 't', 'e', 'r'); }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        // Handle MIDI input to switch output
        for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const MidiEvent& event = midiEvents[i];
            if (event.data[0] == 0x90) // Note On
            {
                // Simple toggle logic based on note
                state_.activeOutput = (event.data[1] % 2);
            }
        }

        downspout::gater::AudioBlock audio;
        audio.inputL = inputs[0];
        audio.inputR = inputs[1];
        audio.output1L = outputs[0];
        audio.output1R = outputs[1];
        audio.output2L = outputs[2];
        audio.output2R = outputs[3];

        downspout::gater::processBlock(state_, params_, {}, frames, getSampleRate(), audio);
    }

private:
    downspout::gater::Parameters params_ {};
    downspout::gater::EngineState state_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GaterPlugin)
};

Plugin* createPlugin() { return new GaterPlugin(); }

END_NAMESPACE_DISTRHO
