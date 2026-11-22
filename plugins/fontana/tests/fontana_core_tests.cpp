#include "fontana_core_types.hpp"
#include "fontana_engine.hpp"
#include "fontana_params.hpp"
#include "fontana_sample_loader.hpp"
#include "fontana_serialization.hpp"
#include "fontana_slices.hpp"
#include "fontana_soundfont.hpp"

#include "downspout/test_assert.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace downspout::fontana;

namespace {

void putU16(std::vector<std::uint8_t>& out, const std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void putU32(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendChunk(std::vector<std::uint8_t>& out, const char* id, const std::vector<std::uint8_t>& data)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(id[i]));
    putU32(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    if ((data.size() & 1u) != 0u)
        out.push_back(0);
}

void appendList(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> list;
    for (int i = 0; i < 4; ++i)
        list.push_back(static_cast<std::uint8_t>(type[i]));
    list.insert(list.end(), body.begin(), body.end());
    appendChunk(out, "LIST", list);
}

std::vector<std::uint8_t> makeWav(const int frames, const int channels, const int sampleRate)
{
    std::vector<std::uint8_t> fmt;
    putU16(fmt, 1);  // PCM
    putU16(fmt, static_cast<std::uint16_t>(channels));
    putU32(fmt, static_cast<std::uint32_t>(sampleRate));
    putU32(fmt, static_cast<std::uint32_t>(sampleRate * channels * 2));
    putU16(fmt, static_cast<std::uint16_t>(channels * 2));
    putU16(fmt, 16);

    std::vector<std::uint8_t> data;
    for (int frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<std::int16_t>(20000.0 * std::sin(2.0 * 3.14159265 * 220.0 * frame / sampleRate));
        for (int channel = 0; channel < channels; ++channel) {
            data.push_back(static_cast<std::uint8_t>(value & 0xffu));
            data.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
        }
    }

    std::vector<std::uint8_t> smpl;
    putU32(smpl, 0);  // manufacturer
    putU32(smpl, 0);  // product
    putU32(smpl, 1000000000u / static_cast<std::uint32_t>(sampleRate));
    putU32(smpl, 60);  // unity note
    putU32(smpl, 0);  // pitch fraction
    putU32(smpl, 0);  // SMPTE format
    putU32(smpl, 0);  // SMPTE offset
    putU32(smpl, 1);  // one loop
    putU32(smpl, 0);  // sampler data
    // Loop record: identifier, type, start, end, fraction, play count.
    putU32(smpl, 0);   // identifier
    putU32(smpl, 0);   // loop type: forward
    putU32(smpl, 10);  // start
    putU32(smpl, 100); // end
    putU32(smpl, 0);   // fraction
    putU32(smpl, 0);   // play count

    std::vector<std::uint8_t> riff;
    riff.push_back('W');
    riff.push_back('A');
    riff.push_back('V');
    riff.push_back('E');
    appendChunk(riff, "fmt ", fmt);
    appendChunk(riff, "smpl", smpl);
    appendChunk(riff, "data", data);

    std::vector<std::uint8_t> file;
    file.push_back('R');
    file.push_back('I');
    file.push_back('F');
    file.push_back('F');
    putU32(file, static_cast<std::uint32_t>(riff.size() + 4u));
    file.insert(file.end(), riff.begin(), riff.end());
    return file;
}

void fixedField(std::vector<std::uint8_t>& out, const std::string& text, const std::size_t size)
{
    out.insert(out.end(), size, 0);
    std::memcpy(out.data() + out.size() - size, text.data(), std::min(text.size(), size));
}

std::vector<std::uint8_t> makeSoundFont()
{
    std::vector<std::uint8_t> ifil;
    putU16(ifil, 2);
    putU16(ifil, 1);
    std::vector<std::uint8_t> inam;
    fixedField(inam, "Test Bank", 10);
    std::vector<std::uint8_t> infoBody;
    appendChunk(infoBody, "ifil", ifil);
    appendChunk(infoBody, "INAM", inam);

    std::vector<std::uint8_t> smpl;
    for (int i = 0; i < 64; ++i)
        putU16(smpl, static_cast<std::uint16_t>(i < 32 ? 12000 : -12000));
    std::vector<std::uint8_t> sdtaBody;
    appendChunk(sdtaBody, "smpl", smpl);

    std::vector<std::uint8_t> phdr;
    fixedField(phdr, "Test Preset", 20);
    putU16(phdr, 0);  // program
    putU16(phdr, 0);  // bank
    putU16(phdr, 0);  // preset bag index
    putU32(phdr, 0);
    putU32(phdr, 0);
    putU32(phdr, 0);
    fixedField(phdr, "EOP", 20);
    putU16(phdr, 0);
    putU16(phdr, 0);
    putU16(phdr, 1);  // EOP bag index
    putU32(phdr, 0);
    putU32(phdr, 0);
    putU32(phdr, 0);

    std::vector<std::uint8_t> pbag;
    putU16(pbag, 0);
    putU16(pbag, 0);
    putU16(pbag, 2);
    putU16(pbag, 0);

    std::vector<std::uint8_t> pgen;
    putU16(pgen, 41);  // instrument
    putU16(pgen, 0);
    putU16(pgen, 43);  // key range 0..127
    putU16(pgen, 0x7f00);
    putU16(pgen, 60);  // terminal
    putU16(pgen, 0);

    std::vector<std::uint8_t> inst;
    fixedField(inst, "Test Inst", 20);
    putU16(inst, 0);  // bag index
    fixedField(inst, "EOI", 20);
    putU16(inst, 1);  // bag index

    std::vector<std::uint8_t> ibag;
    putU16(ibag, 0);
    putU16(ibag, 0);
    putU16(ibag, 2);
    putU16(ibag, 0);

    std::vector<std::uint8_t> igen;
    putU16(igen, 43);  // key range 60..60
    putU16(igen, 0x3c3c);
    putU16(igen, 53);  // sampleId
    putU16(igen, 0);
    putU16(igen, 60);  // terminal
    putU16(igen, 0);

    std::vector<std::uint8_t> shdr;
    fixedField(shdr, "Test Sample", 20);
    putU32(shdr, 0);
    putU32(shdr, 64);
    putU32(shdr, 0);
    putU32(shdr, 32);
    putU32(shdr, 44100);
    shdr.push_back(60);  // original pitch
    shdr.push_back(0);   // pitch correction
    putU16(shdr, 0);     // link
    putU16(shdr, 1);     // mono
    fixedField(shdr, "EOS", 20);
    for (std::size_t i = 0; i < 26u; ++i)
        shdr.push_back(0);

    std::vector<std::uint8_t> pdtaBody;
    appendChunk(pdtaBody, "phdr", phdr);
    appendChunk(pdtaBody, "pbag", pbag);
    appendChunk(pdtaBody, "pmod", {});
    appendChunk(pdtaBody, "pgen", pgen);
    appendChunk(pdtaBody, "inst", inst);
    appendChunk(pdtaBody, "ibag", ibag);
    appendChunk(pdtaBody, "imod", {});
    appendChunk(pdtaBody, "igen", igen);
    appendChunk(pdtaBody, "shdr", shdr);

    std::vector<std::uint8_t> sfbk;
    sfbk.push_back('s');
    sfbk.push_back('f');
    sfbk.push_back('b');
    sfbk.push_back('k');
    appendList(sfbk, "INFO", infoBody);
    appendList(sfbk, "sdta", sdtaBody);
    appendList(sfbk, "pdta", pdtaBody);

    std::vector<std::uint8_t> file;
    file.push_back('R');
    file.push_back('I');
    file.push_back('F');
    file.push_back('F');
    putU32(file, static_cast<std::uint32_t>(sfbk.size() + 4u));
    file.insert(file.end(), sfbk.begin(), sfbk.end());
    return file;
}

template <typename T>
void writeBytes(const std::filesystem::path& path, const std::vector<T>& bytes)
{
    std::ofstream stream(path, std::ios::binary);
    assert(stream);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size() * sizeof(T)));
}

std::filesystem::path scratchPath(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "downspout_fontana_tests";
    std::filesystem::create_directories(dir);
    return dir / ("fontana-test-" + name);
}

MidiInputEvent noteEvent(const std::uint32_t frame, const std::uint8_t status,
                         const std::uint8_t note, const std::uint8_t velocity)
{
    MidiInputEvent event;
    event.frame = frame;
    event.size = 3;
    event.data[0] = status;
    event.data[1] = note;
    event.data[2] = velocity;
    return event;
}

void testGeneratorsAndParameters()
{
    const GenSet defaults = defaultGenerators();
    assert(defaults.get(Gen::InitialFilterFc) == 13500);
    assert(defaults.get(Gen::InitialAttenuation) == 0);
    assert(defaults.get(Gen::AttackVolEnv) == -12000);
    assert(defaults.get(Gen::ScaleTuning) == 100);
    assert(!defaults.has(Gen::OverridingRootKey));
    assert(!defaults.has(Gen::End));

    GenSet override;
    override.set(Gen::Pan, -250);
    GenSet merged = defaults;
    merged.overlay(override);
    assert(merged.get(Gen::Pan) == -250);
    assert(merged.get(Gen::InitialFilterFc) == 13500);
    assert(!defaultModulators().empty());

    Parameters parameters;
    parameters.masterVolume = 99.0f;
    parameters.bank = 999.0f;
    parameters.program = -2.0f;
    parameters.pitchBendRange = 99.0f;
    parameters.filterCutoff = 1.0f;
    const Parameters clamped = clampParameters(parameters);
    assert(clamped.masterVolume <= 1.0f);
    assert(clamped.bank <= 127.0f);
    assert(clamped.program >= 0.0f);
    assert(clamped.pitchBendRange <= 24.0f);
    assert(clamped.filterCutoff >= 100.0f);

    assert(timecentsToSeconds(-12000) == 0.0);
    assert(std::fabs(timecentsToSeconds(0) - 1.0) < 1e-9);
    assert(std::fabs(centibelsToGain(0) - 1.0f) < 1e-6f);
    assert(centibelsToGain(100) < 0.9f);
    assert(std::fabs(centsToHz(1200.0) / centsToHz(0.0) - 2.0) < 1e-9);
}

void testWavLoadingAndPeaks()
{
    const auto path = scratchPath("sine.wav");
    writeBytes(path, makeWav(1000, 2, 44100));

    const auto loaded = loadSampleSource(path.string());
    assert(loaded.error.empty());
    assert(loaded.source.channels == 2);
    assert(loaded.source.sampleRate == 44100);
    assert(loaded.source.frameCount() == 1000);
    assert(loaded.source.rootNote == 60);
    assert(loaded.source.loopEnabled);
    assert(loaded.source.loopStart == 10);
    assert(loaded.source.loopEnd == 100);
    assert(std::any_of(loaded.source.data.begin(), loaded.source.data.end(),
                       [](const float value) { return std::fabs(value) > 0.1f; }));

    const auto peaks = computePeaks(loaded.source, 64);
    assert(peaks.size() == 64);
    assert(std::any_of(peaks.begin(), peaks.end(), [](const Peak& p) { return p.max > 0.2f; }));
    assert(!describeSource(loaded.source).empty());
}

void testSlicing()
{
    SampleSource source;
    source.channels = 1;
    source.sampleRate = 44100;
    source.rootNote = 60;
    source.data.resize(1000);
    for (std::size_t i = 0; i < source.data.size(); ++i)
        source.data[i] = std::sin(static_cast<double>(i) * 0.05) * 0.5f;

    const SliceSet equal = equalSlices(source, 4, 36);
    assert(equal.slices.size() == 4);
    assert(equal.slices.front().start == 0);
    assert(equal.slices.back().end == 999);
    for (std::size_t i = 1; i < equal.slices.size(); ++i)
        assert(equal.slices[i].start == equal.slices[i - 1].end + 1u);
    assert(sliceIndexForNote(equal, 38, 36) == 2);
    assert(sliceIndexForNote(equal, 99, 36) == -1);

    source.data.assign(4000, 0.0f);
    source.data[1000] = 1.0f;
    source.data[2500] = 1.0f;
    const SliceSet transients = transientSlices(source, 0.5f, 36);
    assert(transients.slices.size() >= 2);
    assert(transients.slices.front().start == 0);
    assert(transients.slices.back().end == 3999);

    const SliceSet normalized = normalizeSlices(equal, 500);
    assert(!normalized.empty());
    for (const Slice& slice : normalized.slices)
        assert(slice.end < 500);
}

void testStateRoundTrips()
{
    SliceSet slices;
    slices.sourcePath = scratchPath("sine.wav").string();
    slices.sampleRate = 44100;
    slices.frameCount = 1000;
    slices.slices = {{0, 249, 60, false}, {250, 999, 61, true}};
    const auto restored = deserializeSliceSet(serializeSliceSet(slices));
    assert(restored.has_value());
    assert(restored->sourcePath == slices.sourcePath);
    assert(restored->slices.size() == 2);
    assert(restored->slices[1].muted);
    assert(restored->slices[1].end == 999);
    assert(!deserializeSliceSet("version=99\n").has_value());

    Parameters parameters;
    parameters.bank = 12.0f;
    parameters.program = 34.0f;
    parameters.sliceBaseNote = 48.0f;
    const auto restoredParams = deserializeSettings(serializeSettings(parameters));
    assert(restoredParams.has_value());
    assert(restoredParams->bank == 12.0f);
    assert(restoredParams->program == 34.0f);
    assert(restoredParams->sliceBaseNote == 48.0f);
    assert(!deserializeSettings("garbage").has_value());
}

void testSoundFontParsingAndResolution()
{
    const auto path = scratchPath("bank.sf2");
    writeBytes(path, makeSoundFont());
    assert(isSoundFontFile(path.string()));

    const auto loaded = loadSoundFont(path.string());
    assert(loaded.error.empty());
    assert(loaded.bank.name == "Test Bank");
    assert(loaded.bank.versionMajor == 2);
    assert(loaded.bank.presets.size() == 1);
    assert(loaded.bank.instruments.size() == 1);
    assert(loaded.bank.instruments[0].name == "Test Inst");
    assert(loaded.bank.samples.size() == 1);
    assert(loaded.bank.samples[0].valid);
    assert(loaded.bank.samples[0].data.size() == 64);
    assert(loaded.bank.samples[0].loopEnd == 31);

    const ResolvedVoice hit = resolveNote(loaded.bank, 0, 0, 60, 100);
    assert(hit.found);
    assert(hit.sampleIndex == 0);
    assert(hit.generators.get(Gen::InitialFilterFc) == 13500);
    assert(!resolveNote(loaded.bank, 0, 0, 61, 100).found);
    assert(!resolveNote(loaded.bank, 1, 0, 60, 100).found);
}

float peakAbsolute(const float* buffer, const std::size_t frames)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < frames; ++i)
        peak = std::max(peak, std::fabs(buffer[i]));
    return peak;
}

void testSoundFontPlayback()
{
    const auto path = scratchPath("playback.sf2");
    writeBytes(path, makeSoundFont());
    const auto loaded = loadSoundFont(path.string());
    assert(loaded.error.empty());

    Parameters parameters;
    parameters.masterVolume = 1.0f;
    EngineState state;
    activate(state, 44100.0);
    std::array<float, 256> left {};
    std::array<float, 256> right {};
    AudioBlock audio;
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();
    audio.channelCount = 2;

    const MidiInputEvent on = noteEvent(0, 0x90, 60, 120);
    processBlock(state, parameters, &loaded.bank, nullptr, nullptr, 0u, &on, 1,
                 static_cast<std::uint32_t>(left.size()), 44100.0, audio);
    assert(peakAbsolute(left.data(), left.size()) > 0.01f);
    assert(parameters.statusVoices >= 1.0f);
    assert(parameters.statusLastNote == 60.0f);
}

void testEnginePlayback()
{
    const auto path = scratchPath("engine.wav");
    writeBytes(path, makeWav(22050, 1, 44100));
    const auto loaded = loadSampleSource(path.string());
    assert(loaded.error.empty());

    SliceSet slices = equalSlices(loaded.source, 4, 60);
    Parameters parameters;
    parameters.sourceMode = 1.0f;
    parameters.sliceBaseNote = 60.0f;
    parameters.masterVolume = 1.0f;

    EngineState state;
    activate(state, 44100.0);
    std::array<float, 256> left {};
    std::array<float, 256> right {};
    AudioBlock audio;
    audio.outputs[0] = left.data();
    audio.outputs[1] = right.data();
    audio.channelCount = 2;

    const MidiInputEvent on = noteEvent(0, 0x90, 60, 120);
    processBlock(state, parameters, nullptr, &loaded.source, &slices, 0, &on, 1,
                 static_cast<std::uint32_t>(left.size()), 44100.0, audio);
    assert(peakAbsolute(left.data(), left.size()) > 0.01f);
    assert(parameters.statusVoices >= 1.0f);
    assert(parameters.statusLastNote == 60.0f);

    const MidiInputEvent off = noteEvent(0, 0x80, 60, 0);
    processBlock(state, parameters, nullptr, &loaded.source, &slices, 0, &off, 1,
                 static_cast<std::uint32_t>(left.size()), 44100.0, audio);
    for (int block = 0; block < 40; ++block)
        processBlock(state, parameters, nullptr, &loaded.source, &slices, 0, nullptr, 0,
                     static_cast<std::uint32_t>(left.size()), 44100.0, audio);
    assert(peakAbsolute(left.data(), left.size()) < 0.02f);
}

}  // namespace

int main()
{
    testGeneratorsAndParameters();
    testWavLoadingAndPeaks();
    testSlicing();
    testStateRoundTrips();
    testSoundFontParsingAndResolution();
    testSoundFontPlayback();
    testEnginePlayback();
    return 0;
}
