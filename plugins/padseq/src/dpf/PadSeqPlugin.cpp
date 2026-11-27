#include "DistrhoPlugin.hpp"

#include "padseq_core.hpp"
#include "padseq_params.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::padseq;

ParameterEnumerationValue* scaleEnumValues()
{
    static auto* values = [] {
        auto* array = new std::array<ParameterEnumerationValue, kScaleNames.size()> {};
        for (std::size_t i = 0; i < kScaleNames.size(); ++i)
            (*array)[i] = {static_cast<float>(i), kScaleNames[i]};
        return array;
    }();
    return values->data();
}

ParameterEnumerationValue* noteMapEnumValues()
{
    static auto* values = [] {
        auto* array = new std::array<ParameterEnumerationValue, kNoteMapNames.size()> {};
        for (std::size_t i = 0; i < kNoteMapNames.size(); ++i)
            (*array)[i] = {static_cast<float>(i), kNoteMapNames[i]};
        return array;
    }();
    return values->data();
}

ParameterEnumerationValue* clockEnumValues()
{
    static auto* values = [] {
        auto* array = new std::array<ParameterEnumerationValue, kClockModeNames.size()> {};
        for (std::size_t i = 0; i < kClockModeNames.size(); ++i)
            (*array)[i] = {static_cast<float>(i), kClockModeNames[i]};
        return array;
    }();
    return values->data();
}

ParameterEnumerationValue* ledTransportEnumValues()
{
    static auto* values = [] {
        auto* array = new std::array<ParameterEnumerationValue, kLedTransportNames.size()> {};
        for (std::size_t i = 0; i < kLedTransportNames.size(); ++i)
            (*array)[i] = {static_cast<float>(i), kLedTransportNames[i]};
        return array;
    }();
    return values->data();
}

MidiMessage toCoreMidiMessage(const MidiEvent& event)
{
    MidiMessage message {};
    message.frame = event.frame;
    message.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, message.data.size()));
    const std::uint8_t* const bytes = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < message.size; ++i)
        message.data[i] = bytes[i];
    return message;
}

MidiEvent toDpfMidiEvent(const MidiMessage& event)
{
    MidiEvent midi {};
    midi.frame = event.frame;
    midi.size = event.size;
    if (event.size > MidiEvent::kDataSize)
    {
        midi.dataExt = event.data.data();
    }
    else
    {
        for (std::size_t i = 0; i < event.size; ++i)
            midi.data[i] = event.data[i];
        midi.dataExt = nullptr;
    }
    return midi;
}

TransportSnapshot toTransport(const TimePosition& timePos)
{
    TransportSnapshot transport {};
    transport.valid = timePos.bbt.valid;
    transport.playing = timePos.playing;
    if (timePos.bbt.valid)
    {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1) +
                            (timePos.bbt.ticksPerBeat > 0.0
                                 ? timePos.bbt.tick / timePos.bbt.ticksPerBeat
                                 : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.bpm = timePos.bbt.beatsPerMinute;
    }
    return transport;
}

void setEnumeration(Parameter& parameter, ParameterEnumerationValue* values, std::size_t count)
{
    parameter.enumValues.count = static_cast<std::uint8_t>(count);
    parameter.enumValues.restrictedMode = true;
    parameter.enumValues.values = values;
    parameter.enumValues.deleteLater = false;
}

} // namespace

class PadSeqPlugin : public Plugin
{
public:
    PadSeqPlugin()
        : Plugin(kParameterCount, 0, 0)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override
    {
        return "PadSeq";
    }

    const char* getDescription() const override
    {
        return "Paged 8x8 Launchpad step sequencer with scale and GM drum mapping.";
    }

    const char* getMaker() const override
    {
        return "danja";
    }

    const char* getHomePage() const override
    {
        return "https://danja.github.io/downspout/";
    }

    const char* getLicense() const override
    {
        return "MIT";
    }

    uint32_t getVersion() const override
    {
        return d_version(0, 1, 0);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('P', 'd', 'S', 'q');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (!input && index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        const auto id = static_cast<ParamId>(index);
        const auto range = rangeFor(id);
        parameter.hints = kParameterIsAutomatable;
        parameter.ranges.min = range.minimum;
        parameter.ranges.max = range.maximum;
        parameter.ranges.def = range.defaultValue;

        if (range.output)
        {
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            if (range.boolean)
                parameter.hints |= kParameterIsBoolean;
        }
        else if (range.integer)
        {
            parameter.hints |= kParameterIsInteger;
        }
        if (range.boolean && !range.output)
            parameter.hints |= kParameterIsBoolean;
        if (range.trigger)
            parameter.hints |= kParameterIsTrigger;

        const auto cellBase = static_cast<uint32_t>(ParamId::cellStart);
        if (index >= cellBase && index < cellBase + static_cast<uint32_t>(kCellCount))
        {
            const uint32_t cell = index - cellBase;
            parameter.name = String("Step ") + String(static_cast<int>(cell / kGridWidth) + 1) +
                             String(" Note ") + String(static_cast<int>(cell % kGridWidth) + 1);
            parameter.symbol = String("cell_") + String(static_cast<int>(cell));
            return;
        }

        const auto triggerBase = static_cast<uint32_t>(ParamId::triggerStart);
        if (index >= triggerBase && index < triggerBase + static_cast<uint32_t>(kGridWidth))
        {
            const uint32_t note = index - triggerBase;
            parameter.name = String("Trigger Note ") + String(static_cast<int>(note + 1));
            // Keep the saved symbol stable; only the human-readable name changed.
            parameter.symbol = String("trigger_column_") + String(static_cast<int>(note + 1));
            return;
        }

        const auto statusBase = static_cast<uint32_t>(ParamId::statusCellStart);
        if (index >= statusBase && index < statusBase + static_cast<uint32_t>(kGridCellCount))
        {
            const uint32_t cell = index - statusBase;
            parameter.name = String("Status Note ") + String(static_cast<int>(cell / kGridWidth) + 1) +
                             String(" Step ") + String(static_cast<int>(cell % kGridWidth) + 1);
            parameter.symbol = String("status_cell_") + String(static_cast<int>(cell));
            return;
        }

        switch (id)
        {
        case ParamId::steps:
            parameter.name = "Steps";
            parameter.symbol = "steps";
            break;
        case ParamId::scale:
            parameter.name = "Scale";
            parameter.symbol = "scale";
            setEnumeration(parameter, scaleEnumValues(), kScaleNames.size());
            break;
        case ParamId::root:
            parameter.name = "Root Note";
            parameter.symbol = "root_note";
            break;
        case ParamId::noteView:
            parameter.name = "Note View";
            parameter.symbol = "note_view";
            break;
        case ParamId::page:
            parameter.name = "Page";
            parameter.symbol = "page";
            break;
        case ParamId::channel:
            parameter.name = "MIDI Channel";
            parameter.symbol = "midi_channel";
            break;
        case ParamId::noteMap:
            parameter.name = "Note Map";
            parameter.symbol = "note_map";
            setEnumeration(parameter, noteMapEnumValues(), kNoteMapNames.size());
            break;
        case ParamId::clockMode:
            parameter.name = "Clock";
            parameter.symbol = "clock";
            setEnumeration(parameter, clockEnumValues(), kClockModeNames.size());
            break;
        case ParamId::tempo:
            parameter.name = "Free Tempo";
            parameter.symbol = "free_tempo";
            break;
        case ParamId::velocity:
            parameter.name = "Velocity";
            parameter.symbol = "velocity";
            break;
        case ParamId::seed:
            parameter.name = "Random Seed";
            parameter.symbol = "random_seed";
            break;
        case ParamId::randomise:
            parameter.name = "Randomise";
            parameter.symbol = "randomise";
            break;
        case ParamId::clear:
            parameter.name = "Clear / Panic";
            parameter.symbol = "clear_panic";
            break;
        case ParamId::ledFeedback:
            parameter.name = "LED Feedback";
            parameter.symbol = "led_feedback";
            break;
        case ParamId::ledTransport:
            parameter.name = "LED Out";
            parameter.symbol = "led_out";
            setEnumeration(parameter, ledTransportEnumValues(), kLedTransportNames.size());
            break;
        case ParamId::passInput:
            parameter.name = "Pass Input";
            parameter.symbol = "pass_input";
            break;
        case ParamId::statusStep:
            parameter.name = "Current Step";
            parameter.symbol = "status_step";
            break;
        case ParamId::statusPage:
            parameter.name = "Current Page";
            parameter.symbol = "status_page";
            break;
        case ParamId::statusScale:
            parameter.name = "Current Scale";
            parameter.symbol = "status_scale";
            break;
        case ParamId::statusRunning:
            parameter.name = "Running";
            parameter.symbol = "status_running";
            break;
        case ParamId::statusNoteView:
            parameter.name = "Note View Status";
            parameter.symbol = "status_note_view";
            break;
        case ParamId::statusCellStart:
        case ParamId::cellStart:
        case ParamId::triggerStart:
        case ParamId::count:
            break;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return processor_.getParameter(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        processor_.setParameter(index, value);
    }

    void activate() override
    {
        processor_.activate();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        processor_.setSampleRate(newSampleRate);
    }

    void run(const float**, float** outputs, const uint32_t frames,
             const MidiEvent* midiEvents, const uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        std::array<MidiMessage, 256> coreEvents {};
        const uint32_t eventCount = std::min<uint32_t>(midiEventCount, coreEvents.size());
        for (uint32_t i = 0; i < eventCount; ++i)
            coreEvents[i] = toCoreMidiMessage(midiEvents[i]);

        const ProcessResult result = processor_.processBlock(
            frames, toTransport(getTimePosition()), coreEvents.data(), eventCount);
        for (uint32_t i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidiEvent(result.events[i]));
    }

private:
    Processor processor_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadSeqPlugin)
};

Plugin* createPlugin()
{
    return new PadSeqPlugin();
}

END_NAMESPACE_DISTRHO
