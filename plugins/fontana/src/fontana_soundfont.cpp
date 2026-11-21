#include "fontana_soundfont.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// The plugin build adds third_party/stb_vorbis to the include path; the
// relative path keeps this file compilable even when it does not.
#include "../../../third_party/stb_vorbis/stb_vorbis.h"

namespace downspout::fontana {
namespace {

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxRecords = 1'000'000u;
constexpr std::size_t kMaxSampleFrames = 16'000'000u;

constexpr std::size_t kPhdrSize = 38u;
constexpr std::size_t kBagSize = 4u;
constexpr std::size_t kModSize = 10u;
constexpr std::size_t kGenSize = 4u;
constexpr std::size_t kInstSize = 22u;
constexpr std::size_t kInstNameBytes = 20u;
constexpr std::size_t kShdrSize = 46u;

[[nodiscard]] std::uint16_t readU16(const unsigned char* p) noexcept
{
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::int16_t readI16(const unsigned char* p) noexcept
{
    return static_cast<std::int16_t>(readU16(p));
}

[[nodiscard]] std::uint32_t readU32(const unsigned char* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

struct ByteView {
    const unsigned char* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool covers(const std::size_t offset, const std::size_t length) const noexcept
    {
        return data != nullptr && offset <= size && length <= size - offset;
    }
};

struct RiffChunk {
    char id[4] {};
    ByteView data {};
};

[[nodiscard]] bool isChunk(const RiffChunk& chunk, const char* id) noexcept
{
    return std::memcmp(chunk.id, id, 4) == 0;
}

// Bounds-checked RIFF chunk reader. Skips the pad byte for odd-sized chunks.
[[nodiscard]] bool nextChunk(const ByteView& parent, std::size_t& offset, RiffChunk& out) noexcept
{
    if (!parent.covers(offset, 8u))
        return false;
    const unsigned char* p = parent.data + offset;
    std::memcpy(out.id, p, 4);
    const std::uint32_t size = readU32(p + 4);
    const std::uint64_t bodyEnd = static_cast<std::uint64_t>(offset) + 8u + size;
    if (bodyEnd > parent.size)
        return false;
    out.data = ByteView { p + 8u, static_cast<std::size_t>(size) };
    std::uint64_t next = bodyEnd + (size & 1u);
    if (next > parent.size)
        next = parent.size;  // tolerate a missing final pad byte
    offset = static_cast<std::size_t>(next);
    return true;
}

[[nodiscard]] std::string fixedName(const unsigned char* p, const std::size_t maxLength)
{
    std::size_t length = 0;
    while (length < maxLength && p[length] != 0u)
        ++length;
    return std::string(reinterpret_cast<const char*>(p), length);
}

[[nodiscard]] bool recordCount(const ByteView& view, const std::size_t recordSize,
                               std::size_t& count) noexcept
{
    if (view.data == nullptr || recordSize == 0u || view.size % recordSize != 0u)
        return false;
    count = view.size / recordSize;
    return count <= kMaxRecords;
}

// ---- modulators ------------------------------------------------------------

// User-facing operand layout (as specified for Fontana):
//   bits  0-6  source index
//   bit   7    palette (1 = MIDI CC)
//   bits 8-9   direction
//   bits10-11  polarity
//   bits12-13  transform
enum class ModDirection : std::uint16_t { Increasing = 0, Decreasing = 1 };
enum class ModPolarity : std::uint16_t { Unipolar = 0, Bipolar = 1 };
enum class ModTransform : std::uint16_t { Linear = 0, Concave = 1, Convex = 2, Switch = 3 };

[[nodiscard]] constexpr std::uint16_t makeOperand(const std::uint16_t index, const bool midiCc,
                                                  const ModDirection direction,
                                                  const ModPolarity polarity,
                                                  const ModTransform transform) noexcept
{
    return static_cast<std::uint16_t>(
        (index & 0x7fu) |
        (midiCc ? 0x80u : 0u) |
        (static_cast<std::uint16_t>(direction) << 8) |
        (static_cast<std::uint16_t>(polarity) << 10) |
        (static_cast<std::uint16_t>(transform) << 12));
}

[[nodiscard]] Modulator makeModulator(const std::uint16_t source, const Gen destination,
                                      const std::int16_t amount,
                                      const std::uint16_t amountSource)
{
    Modulator mod;
    mod.source = source;
    mod.destination = static_cast<std::uint16_t>(destination);
    mod.amount = amount;
    mod.amountSource = amountSource;
    mod.transform = 0;
    return mod;
}

// ---- zone records ----------------------------------------------------------

struct BagRef {
    std::size_t gen = 0;
    std::size_t mod = 0;
};

[[nodiscard]] bool readBag(const ByteView& bags, const std::size_t index, BagRef& out) noexcept
{
    if (index >= bags.size / kBagSize)
        return false;
    const unsigned char* p = bags.data + index * kBagSize;
    out.gen = readU16(p);
    out.mod = readU16(p + 2u);
    return true;
}

[[nodiscard]] bool parseGenRange(const ByteView& gens, const std::size_t first,
                                 const std::size_t last, Zone& zone)
{
    if (first > last || last > gens.size / kGenSize)
        return false;
    for (std::size_t i = first; i < last; ++i) {
        const unsigned char* p = gens.data + i * kGenSize;
        const std::uint16_t id = readU16(p);
        const std::int16_t amount = readI16(p + 2u);
        const auto generator = static_cast<Gen>(id);
        switch (generator) {
        case Gen::KeyRange:
            zone.keyLow = amount & 0xff;
            zone.keyHigh = (amount >> 8) & 0xff;
            break;
        case Gen::VelRange:
            zone.velocityLow = amount & 0xff;
            zone.velocityHigh = (amount >> 8) & 0xff;
            break;
        case Gen::Instrument:
            zone.instrument = static_cast<int>(static_cast<std::uint16_t>(amount));
            break;
        case Gen::SampleId:
            zone.sample = static_cast<int>(static_cast<std::uint16_t>(amount));
            break;
        default:
            break;
        }
        // Unknown generators are kept for completeness; GenSet::set ignores
        // out-of-range ids, so they cannot corrupt the known ones.
        zone.generators.set(generator, amount);
    }
    return true;
}

// Terminal all-zero modulator records (pmod/imod) are not real modulators.
[[nodiscard]] bool parseModRange(const ByteView& mods, const std::size_t first,
                                 const std::size_t last, std::vector<Modulator>& out)
{
    if (first > last || last > mods.size / kModSize)
        return false;
    for (std::size_t i = first; i < last; ++i) {
        const unsigned char* p = mods.data + i * kModSize;
        Modulator mod;
        mod.source = readU16(p);
        mod.destination = readU16(p + 2u);
        mod.amount = readI16(p + 4u);
        mod.amountSource = readU16(p + 6u);
        mod.transform = readU16(p + 8u);
        if (mod.source == 0u && mod.destination == 0u && mod.amount == 0 &&
            mod.amountSource == 0u && mod.transform == 0u)
            continue;
        out.push_back(mod);
    }
    return true;
}

// ---- headers ---------------------------------------------------------------

struct PresetHeader {
    std::string name;
    std::uint16_t bagIndex = 0;
    int bank = 0;
    int program = 0;
};

struct InstrumentHeader {
    std::string name;
    std::uint16_t bagIndex = 0;
};

struct SampleHeader {
    std::string name;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t startLoop = 0;
    std::uint32_t endLoop = 0;
    std::uint32_t sampleRate = 0;
    int originalPitch = 60;
    int pitchCorrection = 0;
    std::uint16_t sampleType = 0;
};

[[nodiscard]] bool parsePresetHeaders(const ByteView& phdr, std::vector<PresetHeader>& out,
                                      std::string& error)
{
    std::size_t count = 0;
    if (!recordCount(phdr, kPhdrSize, count) || count < 2u) {
        error = "invalid phdr chunk (need at least one preset and EOP)";
        return false;
    }
    out.reserve(count - 1u);
    for (std::size_t i = 0; i + 1u < count; ++i) {
        const unsigned char* p = phdr.data + i * kPhdrSize;
        PresetHeader header;
        header.name = fixedName(p, 20u);
        header.program = readU16(p + 20u);
        header.bank = readU16(p + 22u);
        header.bagIndex = readU16(p + 24u);
        out.push_back(std::move(header));
    }
    return true;
}

[[nodiscard]] bool parseInstrumentHeaders(const ByteView& inst, std::vector<InstrumentHeader>& out,
                                          std::string& error)
{
    std::size_t count = 0;
    if (!recordCount(inst, kInstSize, count) || count < 2u) {
        error = "invalid inst chunk (need at least one instrument and EOI)";
        return false;
    }
    out.reserve(count - 1u);
    for (std::size_t i = 0; i + 1u < count; ++i) {
        const unsigned char* p = inst.data + i * kInstSize;
        InstrumentHeader header;
        // SF2 2.04: sfInst is char[20] name followed by the bag index.
        header.name = fixedName(p, kInstNameBytes);
        header.bagIndex = readU16(p + kInstNameBytes);
        out.push_back(std::move(header));
    }
    return true;
}

[[nodiscard]] bool parseSampleHeaders(const ByteView& shdr, std::vector<SampleHeader>& out,
                                      std::string& error)
{
    std::size_t count = 0;
    if (!recordCount(shdr, kShdrSize, count) || count < 2u) {
        error = "invalid shdr chunk (need at least one sample and EOS)";
        return false;
    }
    out.reserve(count - 1u);
    for (std::size_t i = 0; i + 1u < count; ++i) {
        const unsigned char* p = shdr.data + i * kShdrSize;
        SampleHeader header;
        header.name = fixedName(p, 20u);
        header.start = readU32(p + 20u);
        header.end = readU32(p + 24u);
        header.startLoop = readU32(p + 28u);
        header.endLoop = readU32(p + 32u);
        header.sampleRate = readU32(p + 36u);
        header.originalPitch = static_cast<int>(p[40]);
        header.pitchCorrection = static_cast<int>(static_cast<std::int8_t>(p[41]));
        header.sampleType = readU16(p + 44u);
        out.push_back(std::move(header));
    }
    return true;
}

// ---- sample decoding -------------------------------------------------------

// All sample indices are made relative to this sample and loopEnd is stored
// inclusively: shdr's [startLoop, endLoop) becomes [loopStart, loopEnd].
void setSamplePoints(Sample& sample, const SampleHeader& header, const std::uint32_t frames,
                     const bool compressed, std::vector<std::string>& warnings)
{
    sample.start = 0;
    sample.end = frames > 0u ? frames - 1u : 0u;
    sample.loopStart = 0;
    sample.loopEnd = 0;
    if (frames == 0u)
        return;

    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
    if (compressed) {
        // SF3 carries loop points relative to the decoded Ogg stream already.
        loopStart = header.startLoop;
        loopEnd = header.endLoop > 0u ? header.endLoop - 1u : 0u;
    } else {
        loopStart = header.startLoop >= header.start ? header.startLoop - header.start : 0u;
        loopEnd = header.endLoop > header.start ? header.endLoop - header.start - 1u : 0u;
    }
    const std::uint32_t last = frames - 1u;
    if (loopStart > last || loopEnd > last) {
        warnings.push_back("sample '" + sample.name + "': loop range clamped to sample");
        loopStart = std::min(loopStart, last);
        loopEnd = std::min(loopEnd, last);
    }
    if (loopEnd < loopStart) {
        warnings.push_back("sample '" + sample.name + "': invalid loop disabled");
        loopStart = 0;
        loopEnd = 0;
    }
    sample.loopStart = loopStart;
    sample.loopEnd = loopEnd;
}

[[nodiscard]] bool decodePcmSample(const SampleHeader& header, const ByteView& smpl,
                                   const ByteView& sm24, Sample& sample,
                                   std::vector<std::string>& warnings)
{
    if (header.end <= header.start ||
        static_cast<std::uint64_t>(header.end) * 2u > smpl.size) {
        warnings.push_back("sample '" + sample.name + "': PCM range outside smpl chunk");
        return false;
    }
    const std::uint32_t frames = header.end - header.start;
    if (frames > kMaxSampleFrames) {
        warnings.push_back("sample '" + sample.name + "': unreasonable PCM frame count");
        return false;
    }

    const bool has24 = sm24.data != nullptr && sm24.size >= header.end;
    if (sm24.data != nullptr && !has24)
        warnings.push_back("sample '" + sample.name + "': sm24 shorter than smpl");

    sample.data.assign(frames, 0.0f);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::uint32_t frame = header.start + i;
        const std::int16_t high = readI16(smpl.data + static_cast<std::size_t>(frame) * 2u);
        float value = static_cast<float>(high) / 32768.0f;
        if (has24) {
            const std::int32_t wide = (static_cast<std::int32_t>(high) << 8) |
                                      static_cast<std::int32_t>(sm24.data[frame]);
            value = static_cast<float>(wide) / 8388608.0f;
        }
        sample.data[i] = value;
    }
    setSamplePoints(sample, header, frames, false, warnings);
    return true;
}

[[nodiscard]] bool decodeOggSample(const SampleHeader& header, const ByteView& smpl,
                                   Sample& sample, std::vector<std::string>& warnings)
{
    // SF3 shdr start/end are BYTE offsets into smpl. Each range is one
    // complete Ogg Vorbis stream (not a frame range as in SF2).
    if (header.end <= header.start || header.end > smpl.size) {
        warnings.push_back("sample '" + sample.name + "': compressed range outside smpl chunk");
        return false;
    }
    const std::size_t byteCount = static_cast<std::size_t>(header.end - header.start);
    if (byteCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        warnings.push_back("sample '" + sample.name + "': compressed range is too large");
        return false;
    }
    int error = 0;
    stb_vorbis* decoder = stb_vorbis_open_memory(smpl.data + header.start,
                                                 static_cast<int>(byteCount), &error, nullptr);
    if (decoder == nullptr) {
        warnings.push_back("sample '" + sample.name + "': cannot open Ogg stream");
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(decoder);
    if (info.channels != 1) {
        warnings.push_back("sample '" + sample.name + "': compressed stream is not mono");
        stb_vorbis_close(decoder);
        return false;
    }
    if (info.sample_rate != 0u)
        sample.sampleRate = info.sample_rate;

    float block[4096];
    while (sample.data.size() < kMaxSampleFrames) {
        const std::size_t wanted =
            std::min<std::size_t>(4096u, kMaxSampleFrames - sample.data.size());
        const int got = stb_vorbis_get_samples_float_interleaved(
            decoder, 1, block, static_cast<int>(wanted));
        if (got <= 0)
            break;
        sample.data.insert(sample.data.end(), block, block + got);
    }
    stb_vorbis_close(decoder);

    if (sample.data.empty()) {
        warnings.push_back("sample '" + sample.name + "': Ogg stream decoded no frames");
        return false;
    }
    setSamplePoints(sample, header, static_cast<std::uint32_t>(sample.data.size()), true, warnings);
    return true;
}

}  // namespace

GenSet defaultGenerators() noexcept
{
    GenSet generators;

    // Leave everything else unset: 0 is a meaningful value for generators such
    // as OverridingRootKey and End, and must not be confused with "inherit".
    generators.set(Gen::InitialFilterFc, 13500);
    generators.set(Gen::InitialFilterQ, 0);
    generators.set(Gen::ScaleTuning, 100);

    // SF2 2.04: these envelope/LFO times use -12000 as "zero time".
    constexpr Gen kZeroTimeGens[] = {
        Gen::DelayModLfo,  Gen::DelayVibLfo,  Gen::DelayModEnv,  Gen::DelayVolEnv,
        Gen::AttackModEnv, Gen::AttackVolEnv, Gen::HoldModEnv,   Gen::HoldVolEnv,
        Gen::DecayModEnv,  Gen::DecayVolEnv,  Gen::ReleaseModEnv, Gen::ReleaseVolEnv,
    };
    for (const Gen generator : kZeroTimeGens)
        generators.set(generator, -12000);

    generators.set(Gen::SustainModEnv, 0);
    generators.set(Gen::SustainVolEnv, 0);
    return generators;
}

std::vector<Modulator> defaultModulators()
{
    constexpr std::uint16_t kSourceVelocity = 2u;
    constexpr std::uint16_t kSourceKey = 3u;
    constexpr std::uint16_t kSourceChannelPressure = 13u;
    constexpr std::uint16_t kSourcePitchWheel = 14u;
    constexpr std::uint16_t kCcModWheel = 1u;
    constexpr std::uint16_t kCcExpression = 11u;

    const auto increasing = ModDirection::Increasing;
    const auto unipolar = ModPolarity::Unipolar;
    const std::uint16_t concaveVelocity = makeOperand(
        kSourceVelocity, false, increasing, unipolar, ModTransform::Concave);
    const std::uint16_t linearVelocity = makeOperand(
        kSourceVelocity, false, increasing, unipolar, ModTransform::Linear);
    const std::uint16_t key = makeOperand(
        kSourceKey, false, increasing, unipolar, ModTransform::Linear);
    const std::uint16_t channelPressure = makeOperand(
        kSourceChannelPressure, false, increasing, unipolar, ModTransform::Linear);
    const std::uint16_t modWheel = makeOperand(
        kCcModWheel, true, increasing, unipolar, ModTransform::Linear);
    const std::uint16_t pitchWheel = makeOperand(
        kSourcePitchWheel, false, increasing, ModPolarity::Bipolar, ModTransform::Linear);
    const std::uint16_t expression = makeOperand(
        kCcExpression, true, increasing, unipolar, ModTransform::Concave);

    std::vector<Modulator> mods;
    mods.reserve(7u);
    mods.push_back(makeModulator(concaveVelocity, Gen::InitialAttenuation, -960, 0u));
    mods.push_back(makeModulator(linearVelocity, Gen::InitialFilterFc, -2400, linearVelocity));
    mods.push_back(makeModulator(key, Gen::InitialFilterFc, 1200, 0u));
    mods.push_back(makeModulator(channelPressure, Gen::VibLfoToPitch, 50, 0u));
    mods.push_back(makeModulator(modWheel, Gen::VibLfoToPitch, 50, 0u));
    mods.push_back(makeModulator(pitchWheel, Gen::VibLfoToPitch, 50, 0u));
    mods.push_back(makeModulator(expression, Gen::InitialAttenuation, -960, 0u));
    return mods;
}

bool isSoundFontFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    char header[12] {};
    file.read(header, static_cast<std::streamsize>(sizeof(header)));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header)))
        return false;
    return std::memcmp(header, "RIFF", 4) == 0 && std::memcmp(header + 8, "sfbk", 4) == 0;
}

SoundFontLoadResult loadSoundFont(const std::string& path)
{
    SoundFontLoadResult result;
    SoundFont& bank = result.bank;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        result.error = "cannot open SoundFont: " + path;
        return result;
    }
    const std::streamoff rawSize = file.tellg();
    if (rawSize < 12 || static_cast<std::uint64_t>(rawSize) > kMaxFileBytes) {
        result.error = "invalid or oversized SoundFont file: " + path;
        return result;
    }
    file.seekg(0);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(rawSize));
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(rawSize));
    if (!file) {
        result.error = "short read: " + path;
        return result;
    }

    const ByteView whole { bytes.data(), bytes.size() };
    if (!whole.covers(0u, 12u) || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "sfbk", 4) != 0) {
        result.error = "not a RIFF sfbk SoundFont";
        return result;
    }
    const std::uint32_t declaredSize = readU32(bytes.data() + 4);
    std::size_t topSize = bytes.size();
    if (declaredSize < bytes.size() - 8u)
        topSize = static_cast<std::size_t>(declaredSize) + 8u;
    const ByteView top { bytes.data(), topSize };

    ByteView info {};
    ByteView sdta {};
    ByteView pdta {};
    std::size_t offset = 12u;
    RiffChunk chunk {};
    while (nextChunk(top, offset, chunk)) {
        if (!isChunk(chunk, "LIST") || !chunk.data.covers(0u, 4u))
            continue;
        const ByteView listBody { chunk.data.data + 4u, chunk.data.size - 4u };
        if (std::memcmp(chunk.data.data, "INFO", 4) == 0)
            info = listBody;
        else if (std::memcmp(chunk.data.data, "sdta", 4) == 0)
            sdta = listBody;
        else if (std::memcmp(chunk.data.data, "pdta", 4) == 0)
            pdta = listBody;
    }
    if (info.data == nullptr || sdta.data == nullptr || pdta.data == nullptr) {
        result.error = "SoundFont is missing an INFO, sdta or pdta list";
        return result;
    }

    std::size_t sub = 0;
    while (nextChunk(info, sub, chunk)) {
        if (isChunk(chunk, "ifil") && chunk.data.covers(0u, 4u)) {
            bank.versionMajor = readU16(chunk.data.data);
            bank.versionMinor = readU16(chunk.data.data + 2u);
        } else if (isChunk(chunk, "INAM")) {
            bank.name = fixedName(chunk.data.data, chunk.data.size);
        } else if (isChunk(chunk, "ISNG")) {
            bank.engine = fixedName(chunk.data.data, chunk.data.size);
        } else if (isChunk(chunk, "IENG")) {
            bank.creator = fixedName(chunk.data.data, chunk.data.size);
        }
    }

    ByteView smpl {};
    ByteView sm24 {};
    sub = 0;
    while (nextChunk(sdta, sub, chunk)) {
        if (isChunk(chunk, "smpl"))
            smpl = chunk.data;
        else if (isChunk(chunk, "sm24"))
            sm24 = chunk.data;
    }

    ByteView phdr {};
    ByteView pbag {};
    ByteView pmod {};
    ByteView pgen {};
    ByteView inst {};
    ByteView ibag {};
    ByteView imod {};
    ByteView igen {};
    ByteView shdr {};
    sub = 0;
    while (nextChunk(pdta, sub, chunk)) {
        if (isChunk(chunk, "phdr")) phdr = chunk.data;
        else if (isChunk(chunk, "pbag")) pbag = chunk.data;
        else if (isChunk(chunk, "pmod")) pmod = chunk.data;
        else if (isChunk(chunk, "pgen")) pgen = chunk.data;
        else if (isChunk(chunk, "inst")) inst = chunk.data;
        else if (isChunk(chunk, "ibag")) ibag = chunk.data;
        else if (isChunk(chunk, "imod")) imod = chunk.data;
        else if (isChunk(chunk, "igen")) igen = chunk.data;
        else if (isChunk(chunk, "shdr")) shdr = chunk.data;
    }
    const auto require = [&result](const ByteView& view, const char* name) -> bool {
        if (view.data == nullptr) {
            result.error = std::string("SoundFont missing ") + name + " chunk";
            return false;
        }
        return true;
    };
    if (!require(phdr, "phdr") || !require(pbag, "pbag") || !require(pgen, "pgen") ||
        !require(inst, "inst") || !require(ibag, "ibag") || !require(igen, "igen") ||
        !require(shdr, "shdr"))
        return result;
    // pmod/imod may legitimately be empty; a bank with its own modulators
    // overrides the engine's defaults (see defaultModulators()).

    std::size_t pbagCount = 0;
    std::size_t ibagCount = 0;
    if (!recordCount(pbag, kBagSize, pbagCount) || pbagCount < 2u ||
        !recordCount(ibag, kBagSize, ibagCount) || ibagCount < 2u) {
        result.error = "invalid preset or instrument bag chunk";
        return result;
    }
    std::size_t phdrCount = 0;
    std::size_t instCount = 0;
    if (!recordCount(phdr, kPhdrSize, phdrCount) || phdrCount < 2u ||
        !recordCount(inst, kInstSize, instCount) || instCount < 2u) {
        result.error = "invalid preset or instrument header chunk";
        return result;
    }
    const std::uint16_t presetBagEnd = readU16(phdr.data + (phdrCount - 1u) * kPhdrSize + 24u);
    const std::uint16_t instBagEnd =
        readU16(inst.data + (instCount - 1u) * kInstSize + kInstNameBytes);

    std::vector<PresetHeader> presetHeaders;
    std::vector<InstrumentHeader> instrumentHeaders;
    std::vector<SampleHeader> sampleHeaders;
    if (!parsePresetHeaders(phdr, presetHeaders, result.error) ||
        !parseInstrumentHeaders(inst, instrumentHeaders, result.error) ||
        !parseSampleHeaders(shdr, sampleHeaders, result.error))
        return result;

    bank.presets.reserve(presetHeaders.size());
    for (std::size_t i = 0; i < presetHeaders.size(); ++i) {
        const PresetHeader& header = presetHeaders[i];
        const std::uint16_t nextBag = i + 1u < presetHeaders.size()
                                          ? presetHeaders[i + 1u].bagIndex
                                          : presetBagEnd;
        if (nextBag < header.bagIndex || nextBag >= pbagCount) {
            result.error = "preset bag indices are inconsistent";
            return result;
        }
        Preset preset;
        preset.name = header.name;
        preset.bank = header.bank;
        preset.program = header.program;
        for (std::size_t z = header.bagIndex; z < nextBag; ++z) {
            BagRef first {};
            BagRef second {};
            if (!readBag(pbag, z, first) || !readBag(pbag, z + 1u, second)) {
                result.error = "preset bag index out of range";
                return result;
            }
            Zone zone;
            if (!parseGenRange(pgen, first.gen, second.gen, zone) ||
                !parseModRange(pmod, first.mod, second.mod, zone.modulators)) {
                result.error = "preset generator or modulator index out of range";
                return result;
            }
            zone.global = !zone.generators.has(Gen::Instrument);
            preset.zones.push_back(std::move(zone));
        }
        bank.presets.push_back(std::move(preset));
    }

    bank.instruments.reserve(instrumentHeaders.size());
    for (std::size_t i = 0; i < instrumentHeaders.size(); ++i) {
        const InstrumentHeader& header = instrumentHeaders[i];
        const std::uint16_t nextBag = i + 1u < instrumentHeaders.size()
                                          ? instrumentHeaders[i + 1u].bagIndex
                                          : instBagEnd;
        if (nextBag < header.bagIndex || nextBag >= ibagCount) {
            result.error = "instrument bag indices are inconsistent";
            return result;
        }
        Instrument instrument;
        instrument.name = header.name;
        for (std::size_t z = header.bagIndex; z < nextBag; ++z) {
            BagRef first {};
            BagRef second {};
            if (!readBag(ibag, z, first) || !readBag(ibag, z + 1u, second)) {
                result.error = "instrument bag index out of range";
                return result;
            }
            Zone zone;
            if (!parseGenRange(igen, first.gen, second.gen, zone) ||
                !parseModRange(imod, first.mod, second.mod, zone.modulators)) {
                result.error = "instrument generator or modulator index out of range";
                return result;
            }
            zone.global = !zone.generators.has(Gen::SampleId);
            instrument.zones.push_back(std::move(zone));
        }
        bank.instruments.push_back(std::move(instrument));
    }

    // Keep one entry per shdr (including failed ones): instrument zones use
    // sampleId as an index into this vector.
    bank.samples.reserve(sampleHeaders.size());
    for (const SampleHeader& header : sampleHeaders) {
        Sample sample;
        sample.name = header.name;
        sample.sampleRate = header.sampleRate != 0u ? header.sampleRate : 44100u;
        sample.originalPitch = std::clamp(header.originalPitch, 0, 127);
        sample.pitchCorrection = header.pitchCorrection;
        sample.sampleType = header.sampleType;
        sample.compressed = (header.sampleType & 0x10u) != 0u;
        sample.valid = sample.compressed
                           ? decodeOggSample(header, smpl, sample, bank.warnings)
                           : decodePcmSample(header, smpl, sm24, sample, bank.warnings);
        bank.samples.push_back(std::move(sample));
    }

    return result;
}

}  // namespace downspout::fontana
