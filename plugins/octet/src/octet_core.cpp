#include "octet_core.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace downspout::octet {

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

BandType typeOf(const Band& b) noexcept
{
    return static_cast<BandType>(static_cast<int>(std::lround(b.type)));
}

// RBJ cookbook cell with explicit Q.
Biquad cell(BandType type, double f0, double gainDb, double q, double sr) noexcept
{
    const double w0 = 2.0 * kPi * f0 / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow(10.0, gainDb / 40.0);
    double b0, b1, b2, a0, a1, a2;
    switch (type) {
    case BandType::Bell:
        b0 = 1.0 + alpha * A; b1 = -2.0 * cw; b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A; a1 = -2.0 * cw; a2 = 1.0 - alpha / A;
        break;
    case BandType::LowShelf: {
        const double s = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cw + s);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - s;
        break;
    }
    case BandType::HighShelf: {
        const double s = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cw + s);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - s;
        break;
    }
    case BandType::Notch:
        b0 = 1.0; b1 = -2.0 * cw; b2 = 1.0;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case BandType::HighPass12:
    case BandType::HighPass48:
        b0 = (1.0 + cw) * 0.5; b1 = -(1.0 + cw); b2 = (1.0 + cw) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    default:  // low-pass
        b0 = (1.0 - cw) * 0.5; b1 = 1.0 - cw; b2 = (1.0 - cw) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    }
    const double inv = 1.0 / a0;
    return {b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv};
}

std::complex<double> response(const Biquad& c, double w) noexcept
{
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

inline double runCell(SectionState& s, const Biquad& c, double x) noexcept
{
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

}  // namespace

Parameters::Parameters()
{
    // Types and frequencies of a conventional eight-band layout. Cuts start off, so a fresh
    // instance changes nothing.
    static constexpr float kFreq[kBands] = {30.0f, 100.0f, 250.0f, 600.0f, 1500.0f, 4000.0f, 8000.0f, 18000.0f};
    static constexpr BandType kType[kBands] = {BandType::HighPass48, BandType::LowShelf, BandType::Bell, BandType::Bell,
                                               BandType::Bell, BandType::Bell, BandType::HighShelf, BandType::LowPass48};
    for (int i = 0; i < kBands; ++i) {
        Band& b = bands[static_cast<size_t>(i)];
        b.type = static_cast<float>(kType[i]);
        b.freqHz = kFreq[i];
        b.on = (i == 0 || i == kBands - 1) ? 0.0f : 1.0f;
        b.q = (kType[i] == BandType::LowShelf || kType[i] == BandType::HighShelf) ? 0.71f : 1.0f;
    }
}

Band clampBand(const Band& in) noexcept
{
    Band b = in;
    b.on      = in.on >= 0.5f ? 1.0f : 0.0f;
    b.type    = std::round(clampf(in.type, 0.0f, static_cast<float>(kBandTypeCount - 1)));
    b.freqHz  = clampf(in.freqHz, 20.0f, 20000.0f);
    b.gainDb  = clampf(in.gainDb, -18.0f, 18.0f);
    b.q       = clampf(in.q, 0.1f, 18.0f);
    b.channel = std::round(clampf(in.channel, 0.0f, 2.0f));
    return b;
}

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters o = p;
    o.mode = std::round(clampf(p.mode, 0.0f, 2.0f));
    o.outputDb = clampf(p.outputDb, -12.0f, 12.0f);
    for (int i = 0; i < kBands; ++i) o.bands[static_cast<size_t>(i)] = clampBand(p.bands[static_cast<size_t>(i)]);
    return o;
}

bool bandUsesGain(const Band& b) noexcept
{
    const BandType t = typeOf(b);
    return t == BandType::Bell || t == BandType::LowShelf || t == BandType::HighShelf;
}

bool bandUsesQ(const Band& b) noexcept
{
    const BandType t = typeOf(b);
    return bandUsesGain(b) || t == BandType::Notch;
}

bool bandIsActive(const Band& raw) noexcept
{
    const Band b = clampBand(raw);
    if (b.on < 0.5f) return false;
    return !bandUsesGain(b) || std::fabs(b.gainDb) > 0.005f;
}

Sections makeSections(const Band& raw, double sampleRate) noexcept
{
    const Band b = clampBand(raw);
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double f0 = std::min(static_cast<double>(b.freqHz), sr * 0.49);
    const BandType t = typeOf(b);
    Sections s;
    switch (t) {
    case BandType::HighPass48:
    case BandType::LowPass48:
        // 8th-order Butterworth: four sections with Q = 1 / (2 cos((2k-1) pi / 16)), so the
        // response is -3 dB at the cutoff (identical Q = 0.707 sections would be -12 dB there).
        for (int k = 1; k <= 4; ++k) {
            const double q = 1.0 / (2.0 * std::cos((2.0 * k - 1.0) * kPi / 16.0));
            s.bq[static_cast<size_t>(s.count++)] = cell(t, f0, 0.0, q, sr);
        }
        break;
    case BandType::HighPass12:
    case BandType::LowPass12:
        s.bq[static_cast<size_t>(s.count++)] = cell(t, f0, 0.0, 0.70710678118654752, sr);
        break;
    default:
        s.bq[static_cast<size_t>(s.count++)] = cell(t, f0, b.gainDb, b.q, sr);
        break;
    }
    return s;
}

double bandResponseDb(const Band& b, double hz, double sampleRate) noexcept
{
    if (!bandIsActive(b)) return 0.0;
    const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const Sections s = makeSections(b, sr);
    const double w = 2.0 * kPi * std::min(std::max(hz, 1.0), sr * 0.499) / sr;
    double mag = 1.0;
    for (int i = 0; i < s.count; ++i) mag *= std::abs(response(s.bq[static_cast<size_t>(i)], w));
    return 20.0 * std::log10(std::max(mag, 1e-9));
}

double responseDb(const Parameters& p, double hz, double sampleRate) noexcept
{
    double total = 0.0;
    for (const Band& b : p.bands) total += bandResponseDb(b, hz, sampleRate);
    return total;
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
}

void processBlock(EngineState& state, const Parameters& rawParams, float sampleRate, uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept
{
    const Parameters p = clampParameters(rawParams);
    const double sr = sampleRate > 0.0f ? sampleRate : 44100.0;
    const StereoMode mode = static_cast<StereoMode>(static_cast<int>(p.mode));

    const float outGoal = std::pow(10.0f, p.outputDb / 20.0f);
    if (!state.primed) {
        state.outputLinear = outGoal;
        state.primed = true;
    }
    const float outFrom = state.outputLinear;
    const float invFrames = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;

    // Work in the output buffers; in-place hosts hand us the same pointers.
    const float* in0 = inputs[0];
    const float* in1 = inputs[1] ? inputs[1] : inputs[0];
    float* out0 = outputs[0];
    float* out1 = outputs[1];
    for (uint32_t i = 0; i < frames; ++i) {
        const float a = std::isfinite(in0[i]) ? in0[i] : 0.0f;
        const float b = std::isfinite(in1[i]) ? in1[i] : 0.0f;
        out0[i] = a;
        out1[i] = b;
    }

    bool any = false;
    for (int bi = 0; bi < kBands; ++bi) {
        BandRuntime& br = state.bands[static_cast<size_t>(bi)];
        const Band& b = p.bands[static_cast<size_t>(bi)];
        const bool active = bandIsActive(b);
        if (active) {
            const bool same = br.computed && b.type == br.last.type && b.freqHz == br.last.freqHz &&
                              b.gainDb == br.last.gainDb && b.q == br.last.q;
            if (!same) {
                br.sections = makeSections(b, sr);
                br.last = b;
                br.computed = true;
            }
        } else if (br.active) {
            br.state = {};  // switched off: drop the memory so re-enabling starts clean
            br.computed = false;
        }
        br.active = active;
        const Channel ch = mode == StereoMode::Stereo ? Channel::Both : static_cast<Channel>(static_cast<int>(b.channel));
        br.channels = ch == Channel::Both ? 3 : (ch == Channel::A ? 1 : 2);
        any = any || active;
    }

    const float s2 = 0.70710678118654752f;
    const bool matrix = any && mode == StereoMode::MidSide;
    if (matrix) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = out0[i], r = out1[i];
            out0[i] = (l + r) * s2;
            out1[i] = (l - r) * s2;
        }
    }

    for (int bi = 0; bi < kBands; ++bi) {
        BandRuntime& br = state.bands[static_cast<size_t>(bi)];
        if (!br.active) continue;
        for (int c = 0; c < 2; ++c) {
            if (!(br.channels & (1 << c))) continue;
            float* buf = c == 0 ? out0 : out1;
            auto& st = br.state[static_cast<size_t>(c)];
            for (uint32_t i = 0; i < frames; ++i) {
                double x = buf[i];
                for (int s = 0; s < br.sections.count; ++s)
                    x = runCell(st[static_cast<size_t>(s)], br.sections.bq[static_cast<size_t>(s)], x);
                buf[i] = static_cast<float>(x);
            }
        }
    }

    if (matrix) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float m = out0[i], s = out1[i];
            out0[i] = (m + s) * s2;
            out1[i] = (m - s) * s2;
        }
    }

    for (uint32_t i = 0; i < frames; ++i) {
        const float g = outFrom + (outGoal - outFrom) * (static_cast<float>(i + 1) * invFrames);
        out0[i] *= g;
        out1[i] *= g;
    }
    state.outputLinear = outGoal;

    // Recursive memory decays into denormals on a silent input; zero what is negligible.
    constexpr double kTiny = 1.0e-30;
    for (BandRuntime& br : state.bands)
        for (auto& ch : br.state)
            for (SectionState& s : ch) {
                if (std::fabs(s.z1) < kTiny) s.z1 = 0.0;
                if (std::fabs(s.z2) < kTiny) s.z2 = 0.0;
            }
}

}  // namespace downspout::octet
