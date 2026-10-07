#include "downspout/test_assert.h"
#include "pratt_params.hpp"

#include <cstdio>

using namespace downspout::pratt;

int main() {
    // The table matches its enum and the output parameters sit after the inputs.
    static_assert(kParameterSpecs.size() == kParameterCount);
    assert(static_cast<std::size_t>(ParamId::outIndex) == kInputParameterCount);
    for (std::size_t i = 0; i < kParameterCount; ++i) {
        const ParamSpec& s = kParameterSpecs[i];
        assert(s.minimum < s.maximum);
        assert(s.defaultValue >= s.minimum && s.defaultValue <= s.maximum);
        assert(s.output == (i >= kInputParameterCount));
        for (std::size_t j = 0; j < i; ++j) assert(std::string(s.symbol) != kParameterSpecs[j].symbol);
    }
    assert(kVoiceNames.size() == kPresetCount + 1);
    assert(static_cast<int>(kParameterSpecs[static_cast<std::size_t>(ParamId::preset)].maximum) ==
           static_cast<int>(kPresetCount));

    // Defaults map to the engine defaults, and 5 * 7 = 35 is the stock index.
    const Settings def;
    const EngineParams ep = toEngineParams(def);
    assert(ep.mode == Mode::Synth && ep.presetOverride == -1 && ep.baseOverride == 0);
    assert(effectiveIndex(def) == 35);
    assert(kParameterSpecs[static_cast<std::size_t>(ParamId::outIndex)].defaultValue == 35.0f);

    // Largest index product stays within the library limit.
    Settings big = def;
    big[ParamId::filterA] = 128.0f;
    big[ParamId::filterB] = 64.0f;
    assert(effectiveIndex(big) == kMaxIndex);

    // Round trip.
    Settings s = def;
    s[ParamId::mode] = 2.0f;
    s[ParamId::preset] = 7.0f;
    s[ParamId::base] = 13.0f;
    s[ParamId::brightness] = 1.75f;
    s[ParamId::cutoff] = 1234.5f;
    s[ParamId::filterA] = 97.0f;
    const auto loaded = deserializeSettings(serializeSettings(s));
    assert(loaded.has_value());
    for (std::size_t i = 0; i < kInputParameterCount; ++i) assert(loaded->v[i] == s.v[i]);

    // Out-of-range input is clamped, integer parameters are rounded.
    std::string text = serializeSettings(def);
    const auto replace = [&](const std::string& key, const std::string& value) {
        const std::size_t at = text.find(key + "=");
        assert(at != std::string::npos);
        const std::size_t end = text.find('\n', at);
        text.replace(at, end - at, key + "=" + value);
    };
    replace("cutoff", "999999");
    replace("filter_a", "5.6");
    replace("mode", "-4");
    const auto clamped = deserializeSettings(text);
    assert(clamped.has_value());
    assert((*clamped)[ParamId::cutoff] == 4000.0f);
    assert((*clamped)[ParamId::filterA] == 6.0f);
    assert((*clamped)[ParamId::mode] == 0.0f);

    // Rejections: bad version, unknown key, duplicate, missing key, garbage value, empty.
    assert(!deserializeSettings("").has_value());
    assert(!deserializeSettings("version=2\n").has_value());
    assert(!deserializeSettings("mode=1\n").has_value());
    const std::string good = serializeSettings(def);
    assert(!deserializeSettings(good + "bogus=1\n").has_value());
    assert(!deserializeSettings(good + "mode=1\n").has_value());
    assert(!deserializeSettings(good.substr(0, good.rfind("mix="))).has_value());
    std::string bad = good;
    bad.replace(bad.find("room=") + 5, 1, "x");
    assert(!deserializeSettings(bad).has_value());
    std::string nan = good;
    nan.replace(nan.find("level=") + 6, 3, "nan");
    assert(!deserializeSettings(nan).has_value());

    std::puts("pratt params tests passed");
    return 0;
}
